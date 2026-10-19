/* nested.c - an inner scope waits before the outer scope resumes.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int inner_done;
static int outer_done;

static void
inner_work (void)
{
  inner_done = 1;
}

static void
outer_work (void)
{
  outer_done = 1;
}

int
main (void)
{
  int after_inner;
  int after_outer;

  scope {
    scope {
      go inner_work ();
    }
    after_inner = inner_done;
    go outer_work ();
  }
  after_outer = inner_done + outer_done;
  crug_task_runtime_shutdown ();

  printf ("scope-nested: after-inner=%d after-outer=%d\n",
	  after_inner, after_outer);
  return (after_inner == 1 && after_outer == 2) ? 0 : 1;
}
