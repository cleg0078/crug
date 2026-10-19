/* stress.c - many scopes, and no task-group state leaks.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdatomic.h>
#include <stdio.h>

#include "task.h"

#define ROUNDS 200
#define PER_ROUND 5

static _Atomic int done;

static void
work (void)
{
  atomic_fetch_add_explicit (&done, 1, memory_order_relaxed);
}

int
main (void)
{
  struct crug_scope_stats groups;
  int expect = 0;

  for (int r = 0; r < ROUNDS; r++)
    {
      scope {
	for (int i = 0; i < PER_ROUND; i++)
	  go work ();
      }
      expect += PER_ROUND;
    }

  crug_task_runtime_shutdown ();
  crug_scope_stats_get (&groups);

  printf ("scope-stress: tasks=%d entered=%llu left=%llu "
	  "leaked task group state: %llu\n", expect,
	  (unsigned long long) groups.entered,
	  (unsigned long long) groups.left,
	  (unsigned long long) groups.live);

  return (done == expect && groups.entered == ROUNDS
	  && groups.left == ROUNDS && groups.live == 0) ? 0 : 1;
}
