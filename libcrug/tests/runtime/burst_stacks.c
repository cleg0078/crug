/* burst_stacks.c - a burst of queued tasks must not pin a stack each.

   Spawning is documented as cheap and queue-only ("a task is guaranteed to be
   queued or the process dies"): the stack belongs to a task that runs, not to
   one that waits in the queue.  Before ADR 0008 A-3 every spawn reserved and
   committed a private stack immediately, so a program that spawns a burst
   before it starts receiving (the classic "spawn a worker per pixel" shape)
   paid one stack per queued task and pinned gigabytes of address space for
   tasks the scheduler had not touched.

   The test parks the single worker inside a task that does not yield, so the
   burst stays queued and nothing can be dispatched while the peak is sampled.
   Then the worker is released, the burst drains, and the counters must agree.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#define _POSIX_C_SOURCE 200809L

#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { BURST_TASKS = 20000 };

static _Atomic int release_worker;
static _Atomic uint32_t ran;

/* Hold the only worker so that the tasks spawned after this one stay queued.
   The loop must not yield: yielding would dispatch the queue.  */
static void
hold_worker (void *frame)
{
  (void) frame;
  while (!atomic_load_explicit (&release_worker, memory_order_acquire))
    ;
  atomic_fetch_add_explicit (&ran, 1u, memory_order_relaxed);
}

static void
counted (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&ran, 1u, memory_order_relaxed);
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
  return 0;			/* unknown: the bound check is skipped  */
#endif
}

int
main (void)
{
  struct crug_task_stats stats;
  unsigned long peak;
  uint32_t i;

#if defined(_WIN32)
  _putenv_s ("CRUG_TASKS_STACK", "1048576");
  _putenv_s ("CRUG_TASKS_WORKERS", "1");
#else
  setenv ("CRUG_TASKS_STACK", "1048576", 0);
  setenv ("CRUG_TASKS_WORKERS", "1", 0);
#endif
  crug_task_spawn (hold_worker, NULL, 0);
  for (i = 0; i < BURST_TASKS; i++)
    crug_task_spawn (counted, NULL, 0);
  peak = peak_kb ();
  atomic_store_explicit (&release_worker, 1, memory_order_release);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  printf ("burst-stacks: tasks=%u spawned=%llu completed=%llu lost=%llu"
	  " corrupt=%llu peak=%lu kB\n", BURST_TASKS + 1u,
	  (unsigned long long) stats.spawned,
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost,
	  (unsigned long long) stats.stack_corrupt, peak);
  return stats.spawned == BURST_TASKS + 1u
	 && stats.completed == BURST_TASKS + 1u
	 && atomic_load_explicit (&ran, memory_order_relaxed) == BURST_TASKS + 1u
	 && stats.lost == 0 && stats.stack_corrupt == 0
	 && (peak == 0 || peak < 1048576ul)
	 ? 0 : 1;
}
