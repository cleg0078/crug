/* early.c - a return inside a scope waits for the children first.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdatomic.h>
#include <stdio.h>

#include "task.h"

static _Atomic int marks;

static void
mark (void)
{
  atomic_fetch_add_explicit (&marks, 1, memory_order_relaxed);
}

static int
stop_early (int stop)
{
  scope {
    go mark ();
    go mark ();
    if (stop)
      return 1;
  }
  return 0;
}

int
main (void)
{
  int early = stop_early (1);
  int seen = marks;

  crug_task_runtime_shutdown ();
  printf ("scope-early: early=%d marks=%d\n", early, seen);
  return (early == 1 && seen == 2) ? 0 : 1;
}
