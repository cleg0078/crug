/* chan_handoff.c - an unbuffered channel and a task group, in plain C.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The producer and the consumer meet on a capacity-0 channel: every send
   completes only when the receiver took the value.  The task group of the
   scope API waits for both, so the program never needs to poll.
*/

#include "chan.h"
#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

static _Atomic int64_t sum;

static void
producer (void *frame)
{
  int value;
  (void) frame;
  for (value = 1; value <= 5; value++)
    crug_chan_send (*(void **) frame, &value);
}

static void
consumer (void *frame)
{
  int i;
  (void) frame;
  for (i = 0; i < 5; i++)
    {
      int value = *(int *) crug_chan_recv_ptr (*(void **) frame);
      atomic_fetch_add_explicit (&sum, value, memory_order_relaxed);
    }
}

int
main (void)
{
  void *chan = crug_chan_make (0, sizeof (int));
  void *group;
  void *frame;

  /* One frame carries the channel handle; both tasks receive the same one.  */
  frame = chan;
  group = crug_scope_enter ();
  crug_task_spawn_scoped (group, producer, &frame, sizeof frame);
  crug_task_spawn_scoped (group, consumer, &frame, sizeof frame);
  crug_scope_leave (group);
  crug_task_runtime_shutdown ();

  if (atomic_load_explicit (&sum, memory_order_relaxed) != 15)
    {
      fprintf (stderr, "FAIL: chan-handoff: sum is %lld\n",
	       (long long) atomic_load (&sum));
      return 1;
    }
  printf ("channels-handoff: handoffs=5 sum=%lld\n",
	  (long long) atomic_load (&sum));
  return 0;
}
