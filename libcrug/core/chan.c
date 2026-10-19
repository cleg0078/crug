/* chan.c - the Crug channel runtime, Phase 16 (16A).
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One struct per channel: a small spin lock, a ring of element_size slots
   and the closed flag.  The ring of a capacity-0 channel has one slot; a
   send deposits and then waits until the receiver has taken the value, so
   capacity 0 is a real handoff rather than a one-element buffer.

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
     the value out of the single slot.  A close while waiting ends the wait so
     that a closed channel cannot strand the sender forever.  */
  if (chan->capacity == 0)
    for (;;)
      {
	size_t count;

	chan_lock (chan);
	count = chan->count;
	chan_unlock (chan);
	if (count == 0 || chan_is_closed (chan))
	  break;
	chan_idle ();
      }
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
  void *out;

  if (recv_buffer_size < chan->element_size)
    {
      unsigned char *bigger
	= (unsigned char *) realloc (recv_buffer, chan->element_size);
      if (bigger == NULL)
	chan_fatal ("cannot receive a channel value");
      recv_buffer = bigger;
      recv_buffer_size = chan->element_size;
    }
  out = recv_buffer;

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
	  atomic_fetch_add_explicit (&chan_received, 1u, memory_order_relaxed);
	  took = 1;
	}
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
