/* tasks_main.c - the Phase 14 task runtime suites.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Seven suites: spawn and completion, yield, by-value frame capture, nested
   spawn, statistics, restart after shutdown, and the no-op yield outside a
   task.  The program prints one line and exits non-zero only on a wrong
   observation.  */

#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static int cases;

#define CHECK(cond, ...)                                                      \
  do                                                                          \
    {                                                                         \
      cases++;                                                                \
      if (!(cond))                                                            \
        {                                                                     \
          failures++;                                                         \
          fprintf (stderr, "FAIL: ");                                         \
          fprintf (stderr, __VA_ARGS__);                                      \
          fputc ('\n', stderr);                                              \
        }                                                                     \
    }                                                                         \
  while (0)

static _Atomic uint64_t counter;
static _Atomic uint64_t frame_value;
static _Atomic uint64_t nested_counter;

static void
bump (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&counter, 1u, memory_order_relaxed);
}

static void
record_frame (void *frame)
{
  atomic_store_explicit (&frame_value, *(uint64_t *) frame, memory_order_relaxed);
}

static void
nested (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&nested_counter, 1u, memory_order_relaxed);
}

static void
spawn_nested (void *frame)
{
  uint32_t i;
  (void) frame;
  for (i = 0; i < 10; i++)
    crug_task_spawn (nested, NULL, 0);
}

static void
yielder (void *frame)
{
  uint32_t i;
  (void) frame;
  for (i = 0; i < 3; i++)
    crug_task_yield ();
  atomic_fetch_add_explicit (&counter, 1u, memory_order_relaxed);
}

int
main (void)
{
  struct crug_task_stats stats;
  uint32_t i;

  /* Suite 1: 100 tasks, all complete, the counter sees every one.  */
  atomic_store (&counter, 0);
  for (i = 0; i < 100; i++)
    crug_task_spawn (bump, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  CHECK (stats.spawned == 100, "suite 1 spawned %llu",
         (unsigned long long) stats.spawned);
  CHECK (stats.completed == 100, "suite 1 completed %llu",
         (unsigned long long) stats.completed);
  CHECK (atomic_load (&counter) == 100, "suite 1 counter %llu",
         (unsigned long long) atomic_load (&counter));
  CHECK (stats.lost == 0, "suite 1 lost %llu",
         (unsigned long long) stats.lost);
  CHECK (stats.stack_corrupt == 0, "suite 1 corrupt %llu",
         (unsigned long long) stats.stack_corrupt);

  /* Suite 2: a task that yields three times still completes.  */
  atomic_store (&counter, 0);
  for (i = 0; i < 8; i++)
    crug_task_spawn (yielder, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  CHECK (stats.completed == 108, "suite 2 completed %llu",
         (unsigned long long) stats.completed);
  CHECK (atomic_load (&counter) == 8, "suite 2 counter %llu",
         (unsigned long long) atomic_load (&counter));
  CHECK (stats.yielded >= 24, "suite 2 yielded %llu",
         (unsigned long long) stats.yielded);

  /* Suite 3: the frame is copied at spawn, not read later.  */
  {
    uint64_t slot = 7;
    atomic_store (&frame_value, 0);
    crug_task_spawn (record_frame, &slot, sizeof slot);
    slot = 99;
    crug_task_runtime_shutdown ();
    CHECK (atomic_load (&frame_value) == 7, "suite 3 frame %llu",
           (unsigned long long) atomic_load (&frame_value));
  }

  /* Suite 4: a task may spawn tasks; all eleven complete.  */
  atomic_store (&nested_counter, 0);
  crug_task_spawn (spawn_nested, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  CHECK (stats.completed == 120, "suite 4 completed %llu",
         (unsigned long long) stats.completed);
  CHECK (atomic_load (&nested_counter) == 10, "suite 4 nested %llu",
         (unsigned long long) atomic_load (&nested_counter));

  /* Suite 5: the statistics agree with the work.  */
  CHECK (stats.workers >= 1 && stats.workers <= 8, "suite 5 workers %u",
         (unsigned) stats.workers);
  CHECK (stats.live == 0, "suite 5 live %u", (unsigned) stats.live);
  CHECK (stats.queue_peak >= 1, "suite 5 queue peak %u",
         (unsigned) stats.queue_peak);

  /* Suite 6: the runtime restarts after a shutdown.  */
  atomic_store (&counter, 0);
  for (i = 0; i < 5; i++)
    crug_task_spawn (bump, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  CHECK (atomic_load (&counter) == 5, "suite 6 counter %llu",
         (unsigned long long) atomic_load (&counter));
  CHECK (stats.completed == 125, "suite 6 completed %llu",
         (unsigned long long) stats.completed);

  /* Suite 7: yielding outside a task is a no-op.  */
  crug_task_yield ();
  CHECK (crug_task_current_id () == 0, "suite 7 current id %u",
         (unsigned) crug_task_current_id ());

  printf ("tasks: %d checks, %d failed, workers %u, completed %llu, "
          "yielded %llu, queue peak %u\n",
          cases, failures, (unsigned) stats.workers,
          (unsigned long long) stats.completed,
          (unsigned long long) stats.yielded, (unsigned) stats.queue_peak);
  return failures == 0 ? 0 : 1;
}
