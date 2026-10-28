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

  /* Wait for the group before stopping the runtime: a bare spawn followed
     by an immediate shutdown lets the group's inner scope spawn after the
     stopping claim, which is a crug-CONC-029 fatal by contract.  */
  scope {
    go group ();
  }
  crug_task_runtime_shutdown ();
  seen = inner;

  printf ("scope-in-task: inner=%d\n", seen);
  return seen == 3 ? 0 : 1;
}
