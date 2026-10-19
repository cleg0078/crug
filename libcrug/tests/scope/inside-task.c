/* inside-task.c - a scope inside a task yields while it waits.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int inner;

static void
work (void)
{
  inner++;
}

static void
group (void)
{
  scope {
    go work ();
    go work ();
    go work ();
  }
}

int
main (void)
{
  int seen;

  go group ();
  crug_task_runtime_shutdown ();
  seen = inner;

  printf ("scope-in-task: inner=%d\n", seen);
  return seen == 3 ? 0 : 1;
}
