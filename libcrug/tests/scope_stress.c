/* scope_stress.c - the Phase 15 task-group runtime stress program.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   This program drives the group half of the runtime API directly, without the
   Crug 'scope' statement, so it can be built by the system compiler under the
   sanitizers (scripts/check-tasks-sanitizer.sh).  Each round creates a group,
   spawns a few children into it and leaves it, which waits for every child;
   after many rounds the counters must balance and no group may survive.

   CRUG_SCOPE_ROUNDS and CRUG_SCOPE_CHILDREN change the sizes.
*/

#define _POSIX_C_SOURCE 200809L

#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic uint64_t done;

struct child_frame
{
  uint64_t id;
};

static void
child (void *opaque)
{
  struct child_frame frame = *(struct child_frame *) opaque;

  atomic_fetch_add_explicit (&done, frame.id, memory_order_relaxed);
}

static unsigned long
env_ulong (const char *name, unsigned long fallback)
{
  const char *text = getenv (name);
  char *end;
  unsigned long value;

  if (text == NULL || *text == '\0')
    return fallback;
  value = strtoul (text, &end, 10);
  return (*end == '\0') ? value : fallback;
}

int
main (void)
{
  struct crug_scope_stats groups;
  unsigned long rounds = env_ulong ("CRUG_SCOPE_ROUNDS", 200);
  unsigned long children = env_ulong ("CRUG_SCOPE_CHILDREN", 5);
  uint64_t expected = (uint64_t) rounds * children * (children + 1) / 2;
  int bad = 0;

  for (unsigned long r = 0; r < rounds; r++)
    {
      void *group = crug_scope_enter ();

      for (unsigned long i = 0; i < children; i++)
	{
	  struct child_frame frame;

	  frame.id = i + 1;
	  crug_task_spawn_scoped (group, child, &frame, sizeof frame);
	}
      crug_scope_leave (group);
    }

  crug_task_runtime_shutdown ();
  crug_scope_stats_get (&groups);

  printf ("scope-stress: tasks=%llu entered=%llu left=%llu "
	  "leaked task group state: %llu\n",
	  (unsigned long long) expected,
	  (unsigned long long) groups.entered,
	  (unsigned long long) groups.left,
	  (unsigned long long) groups.live);

  if (done != expected || groups.entered != rounds || groups.left != rounds
      || groups.live != 0)
    bad = 1;
  return bad;
}
