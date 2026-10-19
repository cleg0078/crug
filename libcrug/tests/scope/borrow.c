/* borrow.c - a scoped task borrows a local mutably; the scope waits.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "task.h"

static void
bump (mut int *p)
{
  *p += 1;
}

int
main (void)
{
  own int *n = adopt ((int *) malloc (sizeof (int)), free);
  int seen;

  *n = 41;
  scope {
    go bump (borrow_mut (n));
  }
  /* The mut borrow is over: its task finished before the block ended.  */
  seen = *n;
  drop (n);
  crug_task_runtime_shutdown ();

  printf ("scope-borrow: n=%d\n", seen);
  return seen == 42 ? 0 : 1;
}
