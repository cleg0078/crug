/* channel_fuzz.c - the Phase 22 channel state-machine libFuzzer target.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The fuzz input is a bounded script, not source: byte 0 selects how many
   channels the run makes (1..4), each following pair of bytes selects a
   capacity (1..8), and the remaining pairs are operations - send, receive,
   close, an inspect, the select rotation and an owned receive - on one of the
   channels.  Only readiness-checked sends and receives are issued, so the run
   never blocks; a blocked path belongs to the scheduler harness.  Every
   operation is mirrored in a model ring (the FIFO oracle) and the channel
   stats are compared with the counts the model kept.

   Invariants: no crash; crug_chan_send_ready/crug_chan_recv_ready agree with
   the model; a buffered receive returns the oldest value (FIFO); a closed
   channel drains first and then returns the zeroed sentinel; len and cap
   agree with the model; close is idempotent; created/sent/received deltas
   equal the model and the wait counter never moves.  A broken ring or a
   mismatched readiness check aborts with a FUZZ-INVARIANT diagnostic, which
   libFuzzer reports as a crash.
*/

#define _POSIX_C_SOURCE 200809L

#include "chan.h"
#include "task.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_CHANNELS 4
#define MAX_CAPACITY 8
#define MAX_OPS 64

/* chan.c calls these two task entry points from its idle path.  This harness
   never runs a task, so the main-thread answers are enough and no scheduler
   object is linked.  */
uint32_t
crug_task_current_id (void)
{
  return 0;
}

void
crug_task_yield (void)
{
}

/* The receive path asks the scheduler for a task-private buffer.  This stub
   models the main-thread path (crug_task_recv_scratch returns NULL outside a
   task), so chan.c falls back to its own thread-local buffer; the runtime does
   define the real symbol.  */
void *
crug_task_recv_scratch (size_t need)
{
  (void) need;
  return NULL;
}

static void
invariant_failed (uint32_t op, const char *what)
{
  fprintf (stderr, "FUZZ-INVARIANT: op %u: %s\n", op, what);
  abort ();
}

#define INVARIANT(cond, op, what)                                             \
  do                                                                          \
    {                                                                         \
      if (!(cond))                                                            \
	invariant_failed ((op), (what));                                      \
    }                                                                         \
  while (0)

struct model_channel
{
  void *chan;
  uint64_t pending[MAX_CAPACITY];
  size_t count;
  size_t capacity;
  int closed;
  uint64_t sent;
  uint64_t received;
  uint64_t closes;
};

static uint64_t
mix64 (uint64_t value)
{
  value += 0x9e3779b97f4a7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
  return value ^ (value >> 31);
}

/* Receive one value and check it against the FIFO model.  Returns 1 when
   the channel yielded a value and 0 when it yielded the closed sentinel;
   crug_chan_stats.received counts only the former.  */
static int
received_value (struct model_channel *m, int owned, uint32_t op)
{
  const uint64_t *got;

  got = (const uint64_t *) (owned ? crug_chan_recv_owned (m->chan)
				  : crug_chan_recv_ptr (m->chan));
  INVARIANT (got != NULL, op, "receive returned no buffer");
  if (m->count > 0)
    {
      INVARIANT (*got == m->pending[0], op, "buffered receive is not FIFO");
      memmove (&m->pending[0], &m->pending[1],
	       (m->count - 1) * sizeof m->pending[0]);
      m->count--;
      m->received++;
      return 1;
    }
  INVARIANT (m->closed, op, "empty receive on an open channel was ready");
  INVARIANT (*got == 0, op, "closed empty receive is not the zero sentinel");
  return 0;
}

int
LLVMFuzzerTestOneInput (const uint8_t *data, size_t size)
{
  struct model_channel ch[MAX_CHANNELS];
  struct crug_chan_stats before, after;
  uint64_t sent = 0;
  uint64_t received = 0;
  uint64_t closes = 0;
  size_t nch;
  size_t pos;
  size_t ops;
  size_t i;
  uint32_t op_index;

  if (size < 3)
    return 0;
  crug_chan_stats_get (&before);

  nch = 1 + (data[0] % MAX_CHANNELS);
  if (size < 1 + 2 * nch)
    return 0;
  memset (ch, 0, sizeof ch);
  for (i = 0; i < nch; i++)
    {
      size_t capacity = 1 + (data[1 + 2 * i] % MAX_CAPACITY);
      ch[i].capacity = capacity;
      ch[i].chan = crug_chan_make (capacity, sizeof (uint64_t));
      INVARIANT (ch[i].chan != NULL, 0, "channel creation failed");
    }

  pos = 1 + 2 * nch;
  ops = (size - pos) / 2;
  if (ops > MAX_OPS)
    ops = MAX_OPS;

  for (op_index = 0; op_index < (uint32_t) ops; op_index++)
    {
      uint8_t raw = data[pos];
      uint8_t which = data[pos + 1];
      uint32_t op = raw % 6;
      struct model_channel *m = &ch[which % nch];
      uint64_t value = mix64 (((uint64_t) raw << 8) | (uint64_t) which
			      | ((uint64_t) op_index << 16));
      int ready;

      pos += 2;
      switch (op)
	{
	case 0:			/* send when the ring has room */
	  ready = crug_chan_send_ready (m->chan);
	  INVARIANT (ready == (m->closed || m->count < m->capacity),
		     op, "send-ready disagrees with the model");
	  if (ready && !m->closed)
	    {
	      if (op_index & 1u)
		crug_chan_send_owned (m->chan, &value);
	      else
		crug_chan_send (m->chan, &value);
	      m->pending[m->count++] = value;
	      m->sent++;
	      sent++;
	    }
	  break;

	case 1:			/* receive through the pointer form */
	  ready = crug_chan_recv_ready (m->chan);
	  INVARIANT (ready == (m->count > 0 || m->closed),
		     op, "recv-ready disagrees with the model");
	  if (ready && received_value (m, 0, op))
	    received++;
	  break;

	case 2:			/* close is idempotent */
	  crug_chan_close (m->chan);
	  if (!m->closed)
	    {
	      m->closed = 1;
	      m->closes++;
	      closes++;
	    }
	  break;

	case 3:			/* inspect */
	  INVARIANT (crug_chan_len (m->chan) == m->count,
		     op, "len disagrees with the model");
	  INVARIANT (crug_chan_cap (m->chan) == m->capacity,
		     op, "cap disagrees with the model");
	  break;

	case 4:			/* the select rotation advances once per call */
	  {
	    unsigned first = crug_chan_select_turn ();
	    unsigned second = crug_chan_select_turn ();
	    INVARIANT (second == first + 1u, op, "select turn did not advance");
	  }
	  break;

	default:		/* receive through the owned form */
	  ready = crug_chan_recv_ready (m->chan);
	  INVARIANT (ready == (m->count > 0 || m->closed),
		     op, "owned recv-ready disagrees with the model");
	  if (ready && received_value (m, 1, op))
	    received++;
	  break;
	}

      INVARIANT (crug_chan_len (m->chan) == m->count,
		 op, "len changed unexpectedly");
    }

  crug_chan_stats_get (&after);
  INVARIANT (after.created - before.created == nch, 0,
	     "created counter disagrees with the model");
  INVARIANT (after.sent - before.sent == sent, 0,
	     "sent counter disagrees with the model");
  INVARIANT (after.received - before.received == received, 0,
	     "received counter disagrees with the model");
  INVARIANT (after.wait == before.wait, 0,
	     "a readiness-checked run waited");
  INVARIANT (after.live == after.created, 0, "the channel registry shrank");
  (void) closes;
  return 0;
}
