
/* owned.c - 'go take (move (buf));' hands the ownership to the task.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "task.h"

static int seen;

static void
take (own char *p)
{
  seen = p[0];
  release (p);
}

int
main (void)
{
  own char *buf = malloc (4);

  buf[0] = 'Q';
  go take (move (buf));
  crug_task_runtime_shutdown ();

  printf ("goroutines-owned: seen=%c\n", seen);

  return seen == 'Q' ? 0 : 1;
}
