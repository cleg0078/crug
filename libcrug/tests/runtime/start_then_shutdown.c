/* start_then_shutdown.c - a fresh start can be stopped at once.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   There is no public "start" entry point (ABI 1): spawn_common() starts the
   runtime lazily for the first spawn.  S-01 defers the publication of
   "started" until the triggering spawn is admitted, so this sequential
   witness is the explicit-start case the audit asked for: the spawn returns
   and the same thread immediately claims the shutdown.  The stop must report
   0 (not stop_failed and not "not started"), the admitted task must have run,
   a second call must report 1 (already stopped) and a later spawn must be
   able to start and stop the runtime again (start_state reset to 0).  If
   start_state were left at 1 the stop would wait forever and the test would
   time out instead of printing.  */

#define _DEFAULT_SOURCE 1
#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int runs;

static void
mark (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&runs, 1u, memory_order_relaxed);
}

int
main (void)
{
  struct crug_task_stats stats;
  int first;
  int second;
  int restart;
  int failed = 0;

  setenv ("CRUG_TASKS_WORKERS", "1", 1);

  /* The first spawn starts the runtime and admits the task in one step.  */
  crug_task_spawn (mark, NULL, 0);
  first = crug_task_runtime_try_shutdown ();
  second = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&stats);

  if (first != 0)
    {
      fprintf (stderr, "FAIL: start-then-shutdown: the first stop returned"
	       " %d (expected 0)\n", first);
      failed = 1;
    }
  if (second != 1)
    {
      fprintf (stderr, "FAIL: start-then-shutdown: the second stop returned"
	       " %d (expected 1)\n", second);
      failed = 1;
    }
  if (stats.lost != 0 || stats.completed != 1
      || atomic_load_explicit (&runs, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: start-then-shutdown: runs=%llu completed=%llu"
	       " lost=%llu\n",
	       (unsigned long long) atomic_load_explicit (&runs,
							  memory_order_acquire),
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  /* start_state must have been reset: a restart stops cleanly again.  */
  crug_task_spawn (mark, NULL, 0);
  restart = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&stats);
  if (restart != 0 || stats.completed != 2 || stats.lost != 0
      || atomic_load_explicit (&runs, memory_order_acquire) != 2)
    {
      fprintf (stderr, "FAIL: start-then-shutdown: restart=%d runs=%llu"
	       " completed=%llu lost=%llu\n", restart,
	       (unsigned long long) atomic_load_explicit (&runs,
							  memory_order_acquire),
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("start-then-shutdown: first=%d second=%d restart=%d runs=%llu"
	  " completed=%llu lost=%llu\n",
	  first, second, restart,
	  (unsigned long long) atomic_load_explicit (&runs,
						     memory_order_acquire),
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost);
  return failed;
}
