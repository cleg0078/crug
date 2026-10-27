/* alloc_bench.c - task allocation/spawn benchmark.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Spawns N no-op tasks and reports the elapsed time and counters.  This is a
   performance probe, not a gate: CI runners vary.  Set CRUG_TASKS_STACK and
   CRUG_TASKS_WORKERS before the first spawn; N is in the environment.

   The spawn loop is timed on its own (spawn_ns) because spawning is queue-only
   since ADR 0008 A-3: a task materializes its stack when a worker dispatches it,
   so a burst of queued tasks costs queue time, not stack time.  peak_kb is the
   process high-water VmPeak in kilobytes on Linux and 0 elsewhere.  */

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

static unsigned long
peak_kb (void)
{
#if defined(__linux__)
  char line[256];
  unsigned long kb = 0;
  FILE *status = fopen ("/proc/self/status", "r");

  if (status == NULL)
    return 0;
  while (fgets (line, sizeof line, status) != NULL)
    if (sscanf (line, "VmPeak: %lu kB", &kb) == 1)
      break;
  fclose (status);
  return kb;
#else
  return 0;
#endif
}

int
main (void)
{
  const char *text = getenv ("N");
  uint64_t n = text != NULL ? strtoull (text, NULL, 10) : 1000000ull;
  struct crug_task_stats stats;
  unsigned long peak;
  uint64_t start;
  uint64_t spawned;
  uint64_t end;
  uint64_t i;

  start = crug_pal_clock_monotonic_ns ();
  for (i = 0; i < n; i++)
    crug_task_spawn (noop, NULL, 0);
  spawned = crug_pal_clock_monotonic_ns ();
  peak = peak_kb ();
  crug_task_runtime_shutdown ();
  end = crug_pal_clock_monotonic_ns ();
  crug_task_stats_get (&stats);

  printf ("alloc-bench: tasks=%llu ns=%llu ns/task=%.2f tasks/s=%.0f "
	  "spawn_ns=%llu spawn_ns/task=%.2f peak_kb=%lu "
	  "completed=%llu lost=%llu corrupt=%llu workers=%u peak=%u\n",
	  (unsigned long long) n,
	  (unsigned long long) (end - start),
	  (double) (end - start) / (double) n,
	  (double) n * 1000000000.0 / (double) (end - start),
	  (unsigned long long) (spawned - start),
	  (double) (spawned - start) / (double) n,
	  peak,
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost,
	  (unsigned long long) stats.stack_corrupt,
	  stats.workers, stats.queue_peak);
  return stats.completed == n && stats.lost == 0 && stats.stack_corrupt == 0
	 ? 0 : 1;
}
