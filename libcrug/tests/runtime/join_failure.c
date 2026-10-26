/* join_failure.c - a worker that cannot be joined leaves the runtime usable.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The fake backend fails the join when CRUG_FAKE_JOIN_FAIL is set, after it
   ran the worker (the same order a real backend would report the wait first).
   The checked shutdown must report 3, keep the worker/started state instead of
   pretending to stop, and report 3 again on the next call.  The process must
   not run the destructor, which would try the void shutdown.  */

#define _DEFAULT_SOURCE 1
#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int ran;

static void
mark (void *frame)
{
  (void) frame;
  atomic_store_explicit (&ran, 1, memory_order_release);
}

static void
fail_join (void)
{
#ifdef _WIN32
  _putenv_s ("CRUG_FAKE_JOIN_FAIL", "1");
#else
  setenv ("CRUG_FAKE_JOIN_FAIL", "1", 1);
#endif
}

int
main (void)
{
  struct crug_task_stats stats;
  int status;

  /* The normal path still joins and completes the task.  */
  crug_task_spawn (mark, NULL, 0);
  status = crug_task_runtime_try_shutdown ();
  if (status != 0
      || atomic_load_explicit (&ran, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: join-failure: the normal shutdown returned %d\n",
	       status);
      return 1;
    }
  crug_task_stats_get (&stats);
  if (stats.completed != 1 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: join-failure: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      return 1;
    }

  /* Restart, then make only the join fail.  */
  crug_task_spawn (mark, NULL, 0);
  fail_join ();
  status = crug_task_runtime_try_shutdown ();
  if (status != 3)
    {
      fprintf (stderr, "FAIL: join-failure: the failed shutdown returned %d\n",
	       status);
      return 1;
    }
  crug_task_stats_get (&stats);
  if (stats.workers != 1 || stats.lost != 0)
    {
      fprintf (stderr,
	       "FAIL: join-failure: the failure did not preserve the state"
	       " (workers=%u lost=%llu)\n",
	       stats.workers, (unsigned long long) stats.lost);
      return 1;
    }

  status = crug_task_runtime_try_shutdown ();
  if (status != 3)
    {
      fprintf (stderr, "FAIL: join-failure: the repeated shutdown returned %d\n",
	       status);
      return 1;
    }

  printf ("join-failure: statuses=3,3 workers=%u completed=%llu\n",
	  stats.workers, (unsigned long long) stats.completed);
  fflush (stdout);
  /* A worker handle was left unjoined by the backend: skip the destructor.  */
  _Exit (0);
}
