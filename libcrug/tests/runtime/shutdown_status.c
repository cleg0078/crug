/* shutdown_status.c - the checked shutdown reports every documented status.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Status 1 (not started), 2 (called from a task, nothing changed) and the
   restart after a successful shutdown are all deterministic without timing:
   the fake and the Linux backend both run the probe task before the owner
   returns from try_shutdown().  */

#define _DEFAULT_SOURCE 1
#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int probe_status = -1;
static _Atomic int probe_ok;
static _Atomic int restarted;

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
probe (void *frame)
{
  struct crug_task_stats before;
  struct crug_task_stats after;
  int status;

  (void) frame;
  crug_task_stats_get (&before);
  status = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&after);
  atomic_store_explicit (&probe_status, status, memory_order_release);
  atomic_store_explicit (&probe_ok,
			 status == 2
			 && before.workers == after.workers
			 && before.live == after.live
			 && before.lost == after.lost
			 && after.live == 1,
			 memory_order_release);
}

static void
restart_task (void *frame)
{
  (void) frame;
  atomic_store_explicit (&restarted, 1, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  int failed = 0;

  set_one_worker ();

  if (crug_task_runtime_try_shutdown () != 1)
    {
      fprintf (stderr, "FAIL: shutdown-status: a stopped runtime reported"
	       " something other than 1\n");
      failed = 1;
    }

  crug_task_spawn (probe, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  if (atomic_load_explicit (&probe_status, memory_order_acquire) != 2)
    {
      fprintf (stderr, "FAIL: shutdown-status: a task got status %d\n",
	       atomic_load_explicit (&probe_status, memory_order_acquire));
      failed = 1;
    }
  if (!atomic_load_explicit (&probe_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: shutdown-status: a task's status-2 call"
	       " changed the runtime state\n");
      failed = 1;
    }
  if (stats.completed != 1 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: shutdown-status: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  /* A stopped runtime is idempotent: another call reports "not started".  */
  if (crug_task_runtime_try_shutdown () != 1)
    {
      fprintf (stderr, "FAIL: shutdown-status: the second stop did not report 1\n");
      failed = 1;
    }

  /* A later spawn starts the runtime again, and the lifetime counters keep
     counting.  */
  crug_task_spawn (restart_task, NULL, 0);
  if (crug_task_runtime_try_shutdown () != 0)
    {
      fprintf (stderr, "FAIL: shutdown-status: the restart did not stop with 0\n");
      failed = 1;
    }
  crug_task_stats_get (&stats);
  if (atomic_load_explicit (&restarted, memory_order_acquire) != 1
      || stats.completed != 2 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: shutdown-status: restarted=%d completed=%llu"
	       " lost=%llu\n",
	       atomic_load_explicit (&restarted, memory_order_acquire),
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("shutdown-status: completed=%llu workers=%u\n",
	  (unsigned long long) stats.completed, stats.workers);
  return failed;
}
