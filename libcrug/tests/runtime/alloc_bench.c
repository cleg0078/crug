/* alloc_bench.c - task allocation/spawn benchmark.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Spawns N no-op tasks and reports the elapsed time and counters.  This is a
   performance probe, not a gate: CI runners vary.  Set CRUG_TASKS_STACK and
   CRUG_TASKS_WORKERS before the first spawn; N is in the environment.  */

#include "crug_pal.h"
#include "task.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void
noop (void *frame)
{
  (void) frame;
}

int
main (void)
{
  const char *text = getenv ("N");
  uint64_t n = text != NULL ? strtoull (text, NULL, 10) : 1000000ull;
  struct crug_task_stats stats;
  uint64_t start;
  uint64_t end;
  uint64_t i;

  start = crug_pal_clock_monotonic_ns ();
  for (i = 0; i < n; i++)
    crug_task_spawn (noop, NULL, 0);
  crug_task_runtime_shutdown ();
  end = crug_pal_clock_monotonic_ns ();
  crug_task_stats_get (&stats);

  printf ("alloc-bench: tasks=%llu ns=%llu ns/task=%.2f tasks/s=%.0f "
	  "completed=%llu lost=%llu corrupt=%llu workers=%u peak=%u\n",
	  (unsigned long long) n,
	  (unsigned long long) (end - start),
	  (double) (end - start) / (double) n,
	  (double) n * 1000000000.0 / (double) (end - start),
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost,
	  (unsigned long long) stats.stack_corrupt,
	  stats.workers, stats.queue_peak);
  return stats.completed == n && stats.lost == 0 && stats.stack_corrupt == 0
	 ? 0 : 1;
}
