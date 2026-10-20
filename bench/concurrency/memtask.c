/* memtask.c - memory reserved per task: pthread-per-task versus Crug.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One process per arm, N small task bodies all kept alive on a gate:

     memtask pthread   N pthreads, default stack size (8 MiB on glibc)
     memtask crug      N Crug tasks, default 64 KiB stack each

   The /proc/self/status VmSize and VmRSS deltas across "all N alive" are the
   measurement; the driver divides them by the task count.  VmSize is the
   *reserved* memory (stacks are reserved, only touched pages are committed),
   so that is the figure the gate compares; VmRSS is reported for context.

   Output, one line per arm:

     mem <arm> vmsize_kb=<delta> rss_kb=<delta> tasks=<n> checksum=<n>
*/

#define _GNU_SOURCE 1

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined (__GLIBC__)
#include <malloc.h>
#endif

#include "task.h"

#define MEM_TASKS 200
#define MEM_RUNS 8

struct meminfo
{
  unsigned long long vmsize_kb;
  unsigned long long vmrss_kb;
};

static _Atomic int gate_open;
static _Atomic int threads_started;
static _Atomic int tasks_started;

/* One no-op task to start the runtime's workers before the baseline is
   sampled: the worker stacks are fixed runtime overhead, not per-task cost.  */
static void
prewarm_task (void *frame)
{
  (void) frame;
}

static int
read_mem (struct meminfo *out)
{
  FILE *f = fopen ("/proc/self/status", "r");
  char line[512];
  unsigned long long value;
  int have_size = 0;
  int have_rss = 0;

  if (f == NULL)
    return -1;
  out->vmsize_kb = 0;
  out->vmrss_kb = 0;
  while (fgets (line, sizeof line, f) != NULL)
    {
      if (sscanf (line, "VmSize: %llu kB", &value) == 1)
	{
	  out->vmsize_kb = value;
	  have_size = 1;
	}
      else if (sscanf (line, "VmRSS: %llu kB", &value) == 1)
	{
	  out->vmrss_kb = value;
	  have_rss = 1;
	}
    }
  fclose (f);
  return have_size && have_rss ? 0 : -1;
}

/* ---- pthread-per-task ------------------------------------------------ */

static void *
mem_thread (void *arg)
{
  (void) arg;
  atomic_fetch_add_explicit (&threads_started, 1, memory_order_acq_rel);
  while (atomic_load_explicit (&gate_open, memory_order_acquire) == 0)
    sched_yield ();
  return NULL;
}

static void
measure_pthread (struct meminfo *delta)
{
  pthread_t threads[MEM_TASKS];
  struct meminfo before, after;
  int i;

  atomic_store_explicit (&gate_open, 0, memory_order_relaxed);
  atomic_store_explicit (&threads_started, 0, memory_order_relaxed);
  if (read_mem (&before) != 0)
    {
      fprintf (stderr, "memtask: cannot read /proc/self/status\n");
      exit (2);
    }
  for (i = 0; i < MEM_TASKS; i++)
    if (pthread_create (&threads[i], NULL, mem_thread, NULL) != 0)
      {
	fprintf (stderr, "memtask: pthread_create failed\n");
	exit (2);
      }
  while (atomic_load_explicit (&threads_started, memory_order_acquire)
	 < MEM_TASKS)
    sched_yield ();
  if (read_mem (&after) != 0)
    {
      fprintf (stderr, "memtask: cannot read /proc/self/status\n");
      exit (2);
    }
  delta->vmsize_kb = after.vmsize_kb > before.vmsize_kb
    ? after.vmsize_kb - before.vmsize_kb : 0;
  delta->vmrss_kb = after.vmrss_kb > before.vmrss_kb
    ? after.vmrss_kb - before.vmrss_kb : 0;
  atomic_store_explicit (&gate_open, 1, memory_order_release);
  for (i = 0; i < MEM_TASKS; i++)
    pthread_join (threads[i], NULL);
}

/* ---- the Crug runtime ------------------------------------------------ */

static void
mem_task (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&tasks_started, 1, memory_order_acq_rel);
  while (atomic_load_explicit (&gate_open, memory_order_acquire) == 0)
    crug_task_yield ();
}

static void
measure_crug (struct meminfo *delta)
{
  struct crug_task_stats before, after;
  struct meminfo m0, m1;
  int i;

  crug_task_stats_get (&before);
  {
    struct crug_task_stats base = before;
    crug_task_spawn (prewarm_task, NULL, 0);
    do
      crug_task_stats_get (&before);
    while (before.completed - base.completed < 1);
  }
  atomic_store_explicit (&gate_open, 0, memory_order_relaxed);
  atomic_store_explicit (&tasks_started, 0, memory_order_relaxed);
  if (read_mem (&m0) != 0)
    {
      fprintf (stderr, "memtask: cannot read /proc/self/status\n");
      exit (2);
    }
  for (i = 0; i < MEM_TASKS; i++)
    crug_task_spawn (mem_task, NULL, 0);
  crug_task_stats_get (&after);
  if (after.live != MEM_TASKS
      || after.spawned - before.spawned != MEM_TASKS)
    {
      fprintf (stderr, "memtask: crug has %u live tasks, expected %d\n",
	       after.live, MEM_TASKS);
      exit (2);
    }
  while (atomic_load_explicit (&tasks_started, memory_order_acquire)
	 < MEM_TASKS)
    sched_yield ();
  if (read_mem (&m1) != 0)
    {
      fprintf (stderr, "memtask: cannot read /proc/self/status\n");
      exit (2);
    }
  delta->vmsize_kb = m1.vmsize_kb > m0.vmsize_kb ? m1.vmsize_kb - m0.vmsize_kb : 0;
  delta->vmrss_kb = m1.vmrss_kb > m0.vmrss_kb ? m1.vmrss_kb - m0.vmrss_kb : 0;
  atomic_store_explicit (&gate_open, 1, memory_order_release);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&after);
  if (after.completed - before.completed != MEM_TASKS
      || after.lost != 0
      || after.stack_corrupt != 0)
    {
      fprintf (stderr,
	       "memtask: crug completed=%llu lost=%llu corrupt=%llu\n",
	       (unsigned long long) (after.completed - before.completed),
	       (unsigned long long) after.lost,
	       (unsigned long long) after.stack_corrupt);
      exit (2);
    }
}

static void
usage (void)
{
  fprintf (stderr, "usage: memtask pthread|crug\n");
}

int
main (int argc, char **argv)
{
  struct meminfo best;
  struct meminfo delta;
  int run;

  if (argc != 2 || (strcmp (argv[1], "pthread") != 0
		    && strcmp (argv[1], "crug") != 0))
    {
      usage ();
      return 2;
    }

#if defined (__GLIBC__)
  /* glibc gives each thread that allocates memory its own 64 MiB PROT_NONE
     arena reservation.  Those reservations come and go in VmSize and would
     swamp the 68 KiB per-task delta this program measures, so route every
     allocation through the main arena.  */
  mallopt (M_ARENA_MAX, 1);
#endif

  best.vmsize_kb = 0;
  best.vmrss_kb = 0;
  for (run = 0; run < MEM_RUNS; run++)
    {
      int attempt = 0;
      do
	{
	  if (strcmp (argv[1], "pthread") == 0)
	    measure_pthread (&delta);
	  else
	    measure_crug (&delta);
	}
      /* A sample below the 64 KiB floor is a measurement artefact (the
	 address space shrank while it was sampled), not a smaller task;
	 take it again instead of recording it as the minimum.  */
      while (delta.vmsize_kb < MEM_TASKS * 64 && ++attempt < 3);
      if (delta.vmsize_kb < MEM_TASKS * 64)
	{
	  fprintf (stderr, "memtask: %s grew VmSize by %llu KiB for %d tasks\n",
		   argv[1], delta.vmsize_kb, MEM_TASKS);
	  return 1;
	}
      if (run == 0 || delta.vmsize_kb < best.vmsize_kb)
	best.vmsize_kb = delta.vmsize_kb;
      if (run == 0 || delta.vmrss_kb < best.vmrss_kb)
	best.vmrss_kb = delta.vmrss_kb;
    }

  printf ("mem %s vmsize_kb=%llu rss_kb=%llu tasks=%d checksum=%d\n",
	  argv[1], best.vmsize_kb, best.vmrss_kb, MEM_TASKS, MEM_TASKS);
  return 0;
}
