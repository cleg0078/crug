/* stats_race.c - crug_task_stats_get is safe to poll while tasks are spawned.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   R-02: queue_peak was a plain uint32_t written under the queue lock and read
   without it by crug_task_stats_get(), so a monitoring thread raced the
   scheduler (a data race that Helgrind/TSan report; the value is monotonic in
   practice, which is why the assertion below is only a sanity check).  The
   field is now _Atomic with a relaxed store under the lock and a relaxed load
   in stats_get; queue_length stays lock-protected.  This program starts the
   runtime before it creates the reader, so the only cross-thread field the
   reader touches while the scheduler mutates it is queue_peak (plus the
   already-atomic counters).  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SPAWN_COUNT 2000u

static _Atomic int stop_reader;
static _Atomic int release_blocker;
static _Atomic int blocker_ran;
static _Atomic int task_runs;
static _Atomic int reader_errors;
static _Atomic unsigned long long samples;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

static void
blocker (void *frame)
{
  (void) frame;
  atomic_store_explicit (&blocker_ran, 1, memory_order_release);
  while (atomic_load_explicit (&release_blocker, memory_order_acquire) == 0)
    crug_task_yield ();
}

static void
one_task (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&task_runs, 1u, memory_order_relaxed);
}

static void
reader (void *arg)
{
  uint32_t last_peak = 0;
  unsigned long long count = 0;

  (void) arg;
  while (atomic_load_explicit (&stop_reader, memory_order_acquire) == 0)
    {
      struct crug_task_stats stats;
      uint64_t spawned;

      crug_task_stats_get (&stats);
      /* Read the monotonic peak before the spawn counter: the peak advances
	 after spawned under the queue lock, so a later read of spawned can be
	 newer than the peak, never older.  */
      if (stats.queue_peak < last_peak)
	{
	  fprintf (stderr, "FAIL: stats-race: queue_peak went backwards"
		   " (%u after %u)\n", stats.queue_peak, last_peak);
	  atomic_store_explicit (&reader_errors, 1, memory_order_release);
	}
      last_peak = stats.queue_peak;
      spawned = stats.spawned;
      if (stats.queue_peak > spawned + 64u)
	{
	  fprintf (stderr, "FAIL: stats-race: queue_peak %u exceeds spawned"
		   " %llu\n", stats.queue_peak,
		   (unsigned long long) spawned);
	  atomic_store_explicit (&reader_errors, 1, memory_order_release);
	}
      if (stats.workers < 1u || stats.workers > CRUG_TASK_WORKERS_MAX)
	{
	  fprintf (stderr, "FAIL: stats-race: workers=%u out of range\n",
		   stats.workers);
	  atomic_store_explicit (&reader_errors, 1, memory_order_release);
	}
      count++;
    }
  atomic_store_explicit (&samples, count, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  crug_pal_thread *reader_thread = NULL;
  unsigned i;
  int status;
  int failed = 0;

  setenv ("CRUG_TASKS_WORKERS", "2", 1);

  /* Start the runtime before the reader exists: configured_workers is written
     once under the queue lock during the first start and is only guaranteed
     ordered for a thread created afterwards.  */
  crug_task_spawn (blocker, NULL, 0);
  for (i = 0; i < 2000u
	      && atomic_load_explicit (&blocker_ran, memory_order_acquire) == 0;
       i++)
    sleep_ms (1);
  if (atomic_load_explicit (&blocker_ran, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: stats-race: the blocker never ran\n");
      return 1;
    }

  if (crug_pal_thread_create (&reader_thread, reader, NULL) != 0)
    {
      fprintf (stderr, "FAIL: stats-race: cannot start the reader\n");
      return 1;
    }

  for (i = 0; i < SPAWN_COUNT; i++)
    crug_task_spawn (one_task, NULL, 0);

  atomic_store_explicit (&stop_reader, 1, memory_order_release);
  if (crug_pal_thread_join (reader_thread) != 0)
    {
      fprintf (stderr, "FAIL: stats-race: cannot join the reader\n");
      return 1;
    }

  atomic_store_explicit (&release_blocker, 1, memory_order_release);
  status = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&stats);

  if (status != 0)
    {
      fprintf (stderr, "FAIL: stats-race: shutdown returned %d\n", status);
      failed = 1;
    }
  if (atomic_load_explicit (&reader_errors, memory_order_acquire) != 0)
    failed = 1;
  if (atomic_load_explicit (&samples, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: stats-race: the reader never sampled\n");
      failed = 1;
    }
  if (stats.completed != SPAWN_COUNT + 1u
      || atomic_load_explicit (&task_runs, memory_order_acquire)
	 != SPAWN_COUNT)
    {
      fprintf (stderr, "FAIL: stats-race: completed=%llu runs=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) atomic_load_explicit (&task_runs,
							  memory_order_acquire));
      failed = 1;
    }
  if (stats.lost != 0)
    {
      fprintf (stderr, "FAIL: stats-race: lost=%llu\n",
	       (unsigned long long) stats.lost);
      failed = 1;
    }
  if (stats.queue_peak < 1u)
    {
      fprintf (stderr, "FAIL: stats-race: queue_peak never advanced\n");
      failed = 1;
    }

  printf ("stats-race: samples=%llu peak=%u spawned=%llu completed=%llu"
	  " lost=%llu workers=%u reader_errors=%d\n",
	  atomic_load_explicit (&samples, memory_order_acquire),
	  stats.queue_peak, (unsigned long long) stats.spawned,
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost, stats.workers,
	  atomic_load_explicit (&reader_errors, memory_order_acquire));
  return failed;
}
