/* tasks_stress.c - the Phase 14 task runtime stress program.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   GOROUTINES_STRESS=1 ./build/tasks-stress runs 100000 trivial tasks; the
   default is 10000 so an ordinary `make check-GOROUTINES` stays quick.  Each
   task keeps a small array on its own stack and checksums it, which is what
   makes a broken context switch visible: a task resumed on the wrong stack, or
   two tasks sharing a stack, changes a checksum.  The program reports the
   counters the Phase 14 gate reads (task loss and stack corruption).
*/

#define _POSIX_C_SOURCE 200809L

#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static _Atomic uint64_t completed;
static _Atomic uint64_t checksum_failures;
static _Atomic uint64_t checksum_total;

struct task_frame
{
  uint64_t id;
  uint64_t pad;
};

static void
trivial (void *opaque)
{
  struct task_frame frame = *(struct task_frame *) opaque;
  unsigned char scratch[256];
  uint64_t sum = 0;
  unsigned i;

  for (i = 0; i < sizeof scratch; i++)
    scratch[i] = (unsigned char) (frame.id + i * 7u);
  if ((frame.id % 3u) == 0)
    crug_task_yield ();
  for (i = 0; i < sizeof scratch; i++)
    sum += scratch[i];
  /* The expected sum is a pure function of the identifier.  */
  {
    uint64_t expected = 0;
    for (i = 0; i < sizeof scratch; i++)
      expected += (unsigned char) (frame.id + i * 7u);
    if (sum != expected)
      atomic_fetch_add_explicit (&checksum_failures, 1u, memory_order_relaxed);
  }
  atomic_fetch_add_explicit (&checksum_total, 1u, memory_order_relaxed);
  atomic_fetch_add_explicit (&completed, 1u, memory_order_relaxed);
}

int
main (void)
{
  const char *stress = getenv ("GOROUTINES_STRESS");
  uint64_t count = (stress != NULL && *stress != '0') ? 100000u : 10000u;
  struct crug_task_stats stats;
  uint64_t i;
  int ok;

  if (getenv ("CRUG_TASKS_STACK") == NULL)
    {
      /* setenv is POSIX; the Windows C library spells it _putenv_s and has
         no setenv at all, so the stress program sets the small stack the way
         each host allows.  */
#if defined(_WIN32)
      _putenv_s ("CRUG_TASKS_STACK", "32768");
#else
      setenv ("CRUG_TASKS_STACK", "32768", 0);
#endif
    }

  for (i = 0; i < count; i++)
    {
      struct task_frame frame;
      frame.id = i;
      frame.pad = ~i;
      crug_task_spawn (trivial, &frame, sizeof frame);
    }
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  ok = (atomic_load (&completed) == count)
       && (stats.completed == count)
       && (stats.spawned == count)
       && (stats.lost == 0)
       && (stats.stack_corrupt == 0)
       && (atomic_load (&checksum_failures) == 0)
       && (atomic_load (&checksum_total) == count);

  printf ("tasks-stress: tasks=%llu completed=%llu checksums=%llu "
          "lost=%llu corrupt=%llu workers=%u queue_peak=%u\n",
          (unsigned long long) count,
          (unsigned long long) stats.completed,
          (unsigned long long) atomic_load (&checksum_total),
          (unsigned long long) stats.lost,
          (unsigned long long) stats.stack_corrupt,
          (unsigned) stats.workers, (unsigned) stats.queue_peak);
  if (!ok)
    fprintf (stderr, "FAIL: task stress invariants do not hold\n");
  return ok ? 0 : 1;
}
