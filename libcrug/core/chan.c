/* chan.c - the Crug channel runtime, Phase 16 (16A).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One struct per channel: a small spin lock, a ring of element_size slots
   and the closed flag.  The ring of a capacity-0 channel has one slot; a
   send deposits a value, then waits until the receiver has taken *that value*
   (a ticket), not merely until the shared slot happens to be empty.  Keying
   the completion to the slot would let a second handoff that is already in
   flight keep the first sender blocked forever; demand-driven tickets are what
   make capacity 0 a real handoff rather than a one-element buffer.

   Blocking never holds the lock: the state transition happens under the
   lock, the wait happens outside it, and every waiter re-tests its
   predicate after coming back (the same discipline as the scheduler, ADR
   0009 D4).  A task waits with crug_task_yield(), the main thread with a
   short PAL sleep, so libcrug/core gains no operating-system primitive.

   Channels are never freed: the registry keeps them reachable, which makes
   a receive after close well defined and keeps a leak checker quiet.  v1
   has no timeouts and no cancellation (docs/crug/CHANNELS.md).
*/

#include "chan.h"

#include "task.h"
#include "task-internal.h"

#include "crug_pal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CRUG_CHAN_MAGIC 0x1c4a95011c4a9501ull
#define CRUG_CHAN_IDLE_NS 50000u

struct crug_chan
{
  uint64_t magic;
  _Atomic uint32_t lock;
  size_t capacity;             /* 0 = one value in flight (handoff)      */
  size_t slots;                /* capacity ? capacity : 1               */
  size_t element_size;
  size_t head;                 /* index of the oldest value             */
  size_t count;                /* values in the ring                    */
  _Atomic uint32_t closed;
  unsigned char *buffer;
  uint64_t sent;
  uint64_t received;
  /* Capacity-0 handoff tickets, all guarded by the channel lock.  A sender
     takes the next ticket and marks it in flight; the receiver that takes the
     value publishes last_consumed, and the sender completes when its own
     ticket has been consumed.  recv_waiters tells a claim send whether a
     receiver is already blocked in a receive, so the claim can deposit and
     wait for that receiver instead of reporting "not ready".  */
  uint64_t next_ticket;
  uint64_t inflight_ticket;
  uint64_t last_consumed;
  _Atomic uint32_t recv_waiters;
  struct crug_chan *next;      /* registry, guarded by registry_lock    */
};

static struct crug_chan *chan_registry;
static _Atomic uint32_t registry_lock;
static _Atomic uint64_t chan_created;
static _Atomic uint64_t chan_sent;
static _Atomic uint64_t chan_received;
static _Atomic uint64_t chan_wait;
static _Atomic unsigned chan_turn;

static _Thread_local unsigned char *recv_buffer;
static _Thread_local size_t recv_buffer_size;

static void
chan_fatal (const char *what)
{
  fprintf (stderr, "crug: %s\n", what);
  abort ();
}

static void
chan_lock (struct crug_chan *chan)
{
  while (atomic_exchange_explicit (&chan->lock, 1u, memory_order_acquire))
    ;
}

static void
chan_unlock (struct crug_chan *chan)
{
  atomic_store_explicit (&chan->lock, 0u, memory_order_release);
}

static int
chan_is_closed (struct crug_chan *chan)
{
  return atomic_load_explicit (&chan->closed, memory_order_acquire) != 0u;
}

/* Give the scheduler a turn: a task yields, the main thread sleeps.  */
static void
chan_idle (void)
{
  atomic_fetch_add_explicit (&chan_wait, 1u, memory_order_relaxed);
  if (crug_task_current_id () != 0u)
    crug_task_yield ();
  else
    crug_pal_sleep_ns (CRUG_CHAN_IDLE_NS);
}

static struct crug_chan *
chan_check (void *handle, const char *who)
{
  struct crug_chan *chan = (struct crug_chan *) handle;

  if (chan == NULL || chan->magic != CRUG_CHAN_MAGIC)
    {
      fprintf (stderr, "crug: %s needs a channel\n", who);
      abort ();
    }
  return chan;
}

/* A claim takes the element size the caller compiled for; a mismatch means
   the program (or the lowering) copied through a type the channel was not
   made with, which cannot be allowed to overflow the destination.  */
static void
chan_check_size (struct crug_chan *chan, size_t size, const char *who)
{
  if (size != chan->element_size)
    {
      fprintf (stderr, "crug: %s size does not match the channel element\n", who);
      abort ();
    }
}

/* The buffer a blocking receive copies through: the calling task's own buffer
   when it runs in a task, the calling thread's buffer on the main thread.  Two
   tasks on one worker must not share it: a task that receives, yields and then
   reads its value would see the other task's element through a thread-local
   buffer.  */
static void *
chan_recv_buffer (struct crug_chan *chan)
{
  void *scratch = crug_task_recv_scratch (chan->element_size);

  if (scratch != NULL)
    return scratch;
  if (recv_buffer_size < chan->element_size)
    {
      unsigned char *bigger
	= (unsigned char *) realloc (recv_buffer, chan->element_size);
      if (bigger == NULL)
	chan_fatal ("cannot receive a channel value");
      recv_buffer = bigger;
      recv_buffer_size = chan->element_size;
    }
  return recv_buffer;
}

/* The receiver side of a capacity-0 handoff: the value just taken is the
   sender's ticket, so publish that under the same lock that took it.  */
static void
chan_consume_ticket (struct crug_chan *chan)
{
  if (chan->capacity == 0)
    {
      chan->last_consumed = chan->inflight_ticket;
      chan->inflight_ticket = 0;
    }
}

/* Wait until the receiver has consumed TICKET; a close before that ends the
   handoff with the diagnostic the contract names.  */
static void
chan_wait_ticket (struct crug_chan *chan, uint64_t ticket)
{
  for (;;)
    {
      uint64_t consumed;
      int closed;

      chan_lock (chan);
      consumed = chan->last_consumed;
      closed = chan_is_closed (chan);
      chan_unlock (chan);
      if (consumed >= ticket)
	return;
      if (closed)
	chan_fatal ("channel closed during handoff");
      chan_idle ();
    }
}

static void
chan_register (struct crug_chan *chan)
{
  while (atomic_exchange_explicit (&registry_lock, 1u, memory_order_acquire))
    ;
  chan->next = chan_registry;
  chan_registry = chan;
  atomic_store_explicit (&registry_lock, 0u, memory_order_release);
  atomic_fetch_add_explicit (&chan_created, 1u, memory_order_relaxed);
}

void *
crug_chan_make (size_t capacity, size_t element_size)
{
  struct crug_chan *chan;
  size_t slots = capacity == 0 ? 1 : capacity;

  if (element_size == 0)
    chan_fatal ("a channel element must have a size");
  if (slots > ((size_t) -1) / element_size)
    chan_fatal ("a channel is too large");

  chan = (struct crug_chan *) calloc (1, sizeof *chan);
  if (chan == NULL)
    chan_fatal ("cannot create a channel");
  chan->buffer = (unsigned char *) calloc (slots, element_size);
  if (chan->buffer == NULL)
    chan_fatal ("cannot create a channel");
  chan->magic = CRUG_CHAN_MAGIC;
  chan->capacity = capacity;
  chan->slots = slots;
  chan->element_size = element_size;
  chan_register (chan);
  return chan;
}

static void
chan_send_common (void *handle, const void *element, const char *who)
{
  struct crug_chan *chan = chan_check (handle, who);
  uint64_t ticket = 0;

  if (element == NULL)
    chan_fatal ("send needs a value");

  for (;;)
    {
      int deposited = 0;

      chan_lock (chan);
      if (chan_is_closed (chan))
	{
	  chan_unlock (chan);
	  chan_fatal ("send on a closed channel");
	}
      if (chan->count < chan->slots)
	{
	  if (chan->capacity == 0)
	    {
	      ticket = ++chan->next_ticket;
	      chan->inflight_ticket = ticket;
	    }
	  memcpy (chan->buffer
		  + ((chan->head + chan->count) % chan->slots)
		  * chan->element_size,
		  element, chan->element_size);
	  chan->count++;
	  chan->sent++;
	  atomic_fetch_add_explicit (&chan_sent, 1u, memory_order_relaxed);
	  deposited = 1;
	}
      chan_unlock (chan);
      if (deposited)
	break;
      chan_idle ();
    }

  /* Capacity 0 is a handoff: the send completes when the receiver has taken
     this value, which it announces by consuming the sender's ticket.  The
     shared slot may already hold the next sender's value by then, and that
     must not keep this sender waiting.  A close before the receiver has taken
     the value ends the handoff with a fatal diagnostic.  */
  if (chan->capacity == 0)
    chan_wait_ticket (chan, ticket);
}

void
crug_chan_send (void *chan, const void *element)
{
  chan_send_common (chan, element, "send");
}

void
crug_chan_send_owned (void *chan, const void *element)
{
  chan_send_common (chan, element, "send");
}

static void *
chan_recv_common (void *handle, const char *who)
{
  struct crug_chan *chan = chan_check (handle, who);
  void *out = chan_recv_buffer (chan);
  int handoff = chan->capacity == 0;

  if (handoff)
    {
      /* Announce the waiter under the lock: a claim send only deposits when a
	 receiver is committed to taking the value.  The count is dropped again
	 in the same critical section that ends the wait, never after it, so a
	 claim cannot deposit into a receive call that is already returning.  */
      chan_lock (chan);
      atomic_fetch_add_explicit (&chan->recv_waiters, 1u, memory_order_acq_rel);
      chan_unlock (chan);
    }

  for (;;)
    {
      int took = 0;
      int closed;

      memset (out, 0, chan->element_size);
      chan_lock (chan);
      closed = chan_is_closed (chan);
      if (chan->count > 0)
	{
	  memcpy (out, chan->buffer + chan->head * chan->element_size,
		  chan->element_size);
	  chan->head = (chan->head + 1) % chan->slots;
	  chan->count--;
	  chan->received++;
	  chan_consume_ticket (chan);
	  atomic_fetch_add_explicit (&chan_received, 1u, memory_order_relaxed);
	  took = 1;
	}
      if ((took || closed) && handoff)
	atomic_fetch_sub_explicit (&chan->recv_waiters, 1u, memory_order_acq_rel);
      chan_unlock (chan);
      if (took || closed)
	return out;
      chan_idle ();
    }
}

void *
crug_chan_recv_ptr (void *chan)
{
  return chan_recv_common (chan, "recv");
}

void *
crug_chan_recv_owned (void *chan)
{
  return chan_recv_common (chan, "recv");
}

int
crug_chan_recv_claim (void *handle, void *out, size_t size)
{
  struct crug_chan *chan = chan_check (handle, "select");
  int status;

  if (out == NULL)
    chan_fatal ("a receive claim needs a value buffer");
  chan_check_size (chan, size, "receive claim");

  chan_lock (chan);
  if (chan->count > 0)
    {
      memcpy (out, chan->buffer + chan->head * chan->element_size,
	      chan->element_size);
      chan->head = (chan->head + 1) % chan->slots;
      chan->count--;
      chan->received++;
      chan_consume_ticket (chan);
      atomic_fetch_add_explicit (&chan_received, 1u, memory_order_relaxed);
      status = 1;
    }
  else if (chan_is_closed (chan))
    {
      memset (out, 0, chan->element_size);
      status = -1;
    }
  else
    status = 0;
  chan_unlock (chan);
  return status;
}

static int
chan_send_claim_common (void *handle, const void *element, size_t size,
			const char *who)
{
  struct crug_chan *chan = chan_check (handle, who);

  if (element == NULL)
    chan_fatal ("a send claim needs a value");
  chan_check_size (chan, size, "send claim");

  chan_lock (chan);
  if (chan_is_closed (chan))
    {
      chan_unlock (chan);
      return -1;
    }
  if (chan->capacity == 0)
    {
      uint64_t ticket;

      /* Do not fill the handoff slot for a receiver that is not there: a
	 select retries, and a deposit nobody consumes would stay in flight
	 (and block a later real handoff).  recv_waiters is the promise that a
	 receiver is inside a receive right now.  */
      if (chan->inflight_ticket != 0
	  || atomic_load_explicit (&chan->recv_waiters,
				   memory_order_acquire) == 0)
	{
	  chan_unlock (chan);
	  return 0;
	}
      ticket = ++chan->next_ticket;
      chan->inflight_ticket = ticket;
      memcpy (chan->buffer
	      + ((chan->head + chan->count) % chan->slots) * chan->element_size,
	      element, chan->element_size);
      chan->count++;
      chan->sent++;
      atomic_fetch_add_explicit (&chan_sent, 1u, memory_order_relaxed);
      chan_unlock (chan);
      chan_wait_ticket (chan, ticket);
      return 1;
    }

  if (chan->count < chan->slots)
    {
      memcpy (chan->buffer
	      + ((chan->head + chan->count) % chan->slots) * chan->element_size,
	      element, chan->element_size);
      chan->count++;
      chan->sent++;
      atomic_fetch_add_explicit (&chan_sent, 1u, memory_order_relaxed);
      chan_unlock (chan);
      return 1;
    }
  chan_unlock (chan);
  return 0;
}

int
crug_chan_send_claim (void *handle, const void *element, size_t size)
{
  return chan_send_claim_common (handle, element, size, "select");
}

int
crug_chan_send_owned_claim (void *handle, const void *element, size_t size)
{
  return chan_send_claim_common (handle, element, size, "select");
}

int
crug_chan_recv_ready (void *handle)
{
  struct crug_chan *chan = chan_check (handle, "select");
  int ready;

  chan_lock (chan);
  ready = chan->count > 0 || chan_is_closed (chan);
  chan_unlock (chan);
  return ready;
}

int
crug_chan_send_ready (void *handle)
{
  struct crug_chan *chan = chan_check (handle, "select");
  int ready;

  /* A closed channel reports ready so that the selected send reaches the
     fatal diagnostic instead of making the select spin forever.  */
  chan_lock (chan);
  ready = chan_is_closed (chan) || chan->count < chan->slots;
  chan_unlock (chan);
  return ready;
}

void
crug_chan_poll_wait (void)
{
  chan_idle ();
}

unsigned
crug_chan_select_turn (void)
{
  return (unsigned) atomic_fetch_add_explicit (&chan_turn, 1u,
                                               memory_order_relaxed);
}

void
crug_chan_close (void *handle)
{
  struct crug_chan *chan = chan_check (handle, "close");

  chan_lock (chan);
  atomic_store_explicit (&chan->closed, 1u, memory_order_release);
  chan_unlock (chan);
}

size_t
crug_chan_len (void *handle)
{
  struct crug_chan *chan = chan_check (handle, "len");
  size_t len;

  chan_lock (chan);
  len = chan->count;
  chan_unlock (chan);
  return len;
}

size_t
crug_chan_cap (void *handle)
{
  return chan_check (handle, "cap")->capacity;
}

void
crug_chan_stats_get (struct crug_chan_stats *out)
{
  if (out == NULL)
    return;
  out->created = atomic_load_explicit (&chan_created, memory_order_relaxed);
  out->live = out->created;
  out->sent = atomic_load_explicit (&chan_sent, memory_order_relaxed);
  out->received = atomic_load_explicit (&chan_received, memory_order_relaxed);
  out->wait = atomic_load_explicit (&chan_wait, memory_order_relaxed);
}
