/* scope_free_race.c - a task group is never freed under a running wake.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   crug_task_finish() drops the last pending count and then wakes the group
   word.  Without the completion handshake a waiter can observe pending==0,
   free the group and let the wake run on freed memory (R-01).  The fix raises
   in_flight before the decrement and drops it after the wake returned, so
   crug_scope_leave() cannot release the group while the wake still refers to
   it (core/task.c, struct crug_scope).

   This is the plain runtime regression for that handshake: it creates and
   leaves many groups, from the main thread (the PAL wait path) and from
   inside a task (the yield path), and checks that every group was released.
   Run it under ASan (scripts/check-tasks-sanitizer.sh) to give the window
   sanitizer coverage; tests/runtime/scope_wake_race.c holds the completion
   wake and proves the wait ordering directly.  */

#define _DEFAULT_SOURCE 1
#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define OUTER_ITERATIONS 2000
#define INNER_ITERATIONS 500

static _Atomic int children_ran;

static void
count_child (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&children_ran, 1, memory_order_relaxed);
}

/* One group entered and left from inside a task: leaving yields to the
   workers instead of blocking one while the child runs.  */
static void
nested (void *frame)
{
  void *inner;

  (void) frame;
  inner = crug_scope_enter ();
  crug_task_spawn_scoped (inner, count_child, NULL, 0);
  crug_scope_leave (inner);
  atomic_fetch_add_explicit (&children_ran, 1, memory_order_relaxed);
}

int
main (void)
{
  struct crug_scope_stats before;
  struct crug_scope_stats after;
  uint64_t expected_children = 2u * OUTER_ITERATIONS + 2u * INNER_ITERATIONS;
  int failed = 0;
  int i;

  setenv ("CRUG_TASKS_WORKERS", "4", 1);
  crug_scope_stats_get (&before);

  /* Main-thread waiter: two children per group.  */
  for (i = 0; i < OUTER_ITERATIONS; i++)
    {
      void *scope = crug_scope_enter ();
      crug_task_spawn_scoped (scope, count_child, NULL, 0);
      crug_task_spawn_scoped (scope, count_child, NULL, 0);
      crug_scope_leave (scope);
    }

  /* Task waiter: leaving from inside a task yields until the inner child
     returned, so a group is also released from the yield path.  */
  for (i = 0; i < INNER_ITERATIONS; i++)
    {
      void *scope = crug_scope_enter ();
      crug_task_spawn_scoped (scope, nested, NULL, 0);
      crug_scope_leave (scope);
    }

  if (crug_task_runtime_try_shutdown () != 0)
    {
      fprintf (stderr, "FAIL: scope-free-race: shutdown did not complete\n");
      failed = 1;
    }

  crug_scope_stats_get (&after);
  if (after.live != 0 || after.entered != after.left)
    {
      fprintf (stderr,
	       "FAIL: scope-free-race: entered=%llu left=%llu live=%llu\n",
	       (unsigned long long) after.entered,
	       (unsigned long long) after.left,
	       (unsigned long long) after.live);
      failed = 1;
    }
  if (after.entered - before.entered
      != (uint64_t) OUTER_ITERATIONS + 2u * (uint64_t) INNER_ITERATIONS)
    {
      fprintf (stderr,
	       "FAIL: scope-free-race: %llu groups created, expected %d\n",
	       (unsigned long long) (after.entered - before.entered),
	       OUTER_ITERATIONS + 2 * INNER_ITERATIONS);
      failed = 1;
    }
  if ((uint64_t) atomic_load_explicit (&children_ran, memory_order_acquire)
      != expected_children)
    {
      fprintf (stderr,
	       "FAIL: scope-free-race: %d children ran, expected %llu\n",
	       atomic_load (&children_ran),
	       (unsigned long long) expected_children);
      failed = 1;
    }

  printf ("scope-free-race: entered=%llu left=%llu live=%llu children=%d"
	  " status=0\n",
	  (unsigned long long) after.entered,
	  (unsigned long long) after.left,
	  (unsigned long long) after.live,
	  atomic_load (&children_ran));
  return failed;
}
