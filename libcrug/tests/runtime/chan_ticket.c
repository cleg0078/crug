/* chan_ticket.c - a capacity-0 send completes when its own value is taken.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   R4-RT-001: the old handoff keyed a sender's completion to the shared slot
   being empty.  When the receiver immediately sent another value into the
   slot, the first sender stayed blocked although its value had been received.
   The ticket counter makes completion follow the value.  One worker keeps the
   interleaving deterministic: the receiver refills the slot before the first
   sender gets to observe whether the handoff completed.  */

#define _DEFAULT_SOURCE 1
#include "chan.h"
#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void *handoff;
static _Atomic int sender_a_done;
static _Atomic int sender_b_done;
static _Atomic int receiver_done;
static _Atomic int receiver_value;
static _Atomic int finisher_done;
static _Atomic int finisher_fail;
static _Atomic int finisher_first_value;
static _Atomic int finisher_first_done;
static _Atomic int finisher_second;

static void
set_one_worker (void)
{
#ifdef _WIN32
  _putenv_s ("CRUG_TASKS_WORKERS", "1");
#else
  setenv ("CRUG_TASKS_WORKERS", "1", 1);
#endif
}

static void
sender_a (void *frame)
{
  int value = 11;
  (void) frame;
  crug_chan_send (handoff, &value);
  atomic_store_explicit (&sender_a_done, 1, memory_order_release);
}

static void
sender_b (void *frame)
{
  int value = 33;
  (void) frame;
  crug_chan_send (handoff, &value);
  atomic_store_explicit (&sender_b_done, 1, memory_order_release);
}

static void
receiver (void *frame)
{
  int value = 22;
  (void) frame;
  atomic_store_explicit (&receiver_value,
			 *(int *) crug_chan_recv_ptr (handoff),
			 memory_order_release);
  crug_chan_send (handoff, &value);
  atomic_store_explicit (&receiver_done, 1, memory_order_release);
}

static void
finisher (void *frame)
{
  int polls;

  (void) frame;

  /* Wait until a sender has observed its own handoff completing.  The buggy
     runtime never lets either sender complete while 22 occupies the slot.  */
  for (polls = 0; polls < 1000000; polls++)
    {
      if (atomic_load_explicit (&sender_a_done, memory_order_acquire)
	  || atomic_load_explicit (&sender_b_done, memory_order_acquire))
	break;
      crug_task_yield ();
    }
  if (!atomic_load_explicit (&sender_a_done, memory_order_acquire)
      && !atomic_load_explicit (&sender_b_done, memory_order_acquire))
    atomic_store_explicit (&finisher_fail, 1, memory_order_release);
  atomic_store_explicit (&finisher_first_done,
			 atomic_load_explicit (&sender_a_done,
					       memory_order_acquire)
			   ? 11
			   : (atomic_load_explicit (&sender_b_done,
						    memory_order_acquire)
			      ? 33 : 0),
			 memory_order_release);

  /* Drain the receiver's 22 and then the other sender's value, so the test
     terminates even when the bug under test is present.  */
  atomic_store_explicit (&finisher_first_value,
			 *(int *) crug_chan_recv_ptr (handoff),
			 memory_order_release);
  atomic_store_explicit (&finisher_second,
			 *(int *) crug_chan_recv_ptr (handoff),
			 memory_order_release);
  atomic_store_explicit (&finisher_done, 1, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  int failed = 0;
  int first;
  int second;
  int received;
  int first_done;

  set_one_worker ();
  handoff = crug_chan_make (0, sizeof (int));
  crug_task_spawn (sender_a, NULL, 0);
  crug_task_spawn (sender_b, NULL, 0);
  crug_task_spawn (receiver, NULL, 0);
  crug_task_spawn (finisher, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  first = atomic_load_explicit (&finisher_first_value, memory_order_acquire);
  second = atomic_load_explicit (&finisher_second, memory_order_acquire);
  received = atomic_load_explicit (&receiver_value, memory_order_acquire);
  first_done = atomic_load_explicit (&finisher_first_done, memory_order_acquire);

  if (atomic_load_explicit (&finisher_fail, memory_order_acquire) != 0)
    {
      fprintf (stderr,
	       "FAIL: chan-ticket: no sender completed while the handoff slot"
	       " held another value\n");
      failed = 1;
    }
  if (!atomic_load_explicit (&sender_a_done, memory_order_acquire)
      || !atomic_load_explicit (&sender_b_done, memory_order_acquire)
      || !atomic_load_explicit (&receiver_done, memory_order_acquire)
      || !atomic_load_explicit (&finisher_done, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: chan-ticket: a task did not complete\n");
      failed = 1;
    }
  if (first != 22)
    {
      fprintf (stderr, "FAIL: chan-ticket: the finisher first saw %d\n", first);
      failed = 1;
    }
  if (!((received == 11 && first_done == 11 && second == 33)
	|| (received == 33 && first_done == 33 && second == 11)))
    {
      fprintf (stderr,
	       "FAIL: chan-ticket: receiver=%d first_done=%d second=%d\n",
	       received, first_done, second);
      failed = 1;
    }
  if (stats.completed != 4 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: chan-ticket: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("chan-ticket: completed=%llu receiver=%d finisher=%d,%d\n",
	  (unsigned long long) stats.completed, received, first, second);
  return failed;
}
