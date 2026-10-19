/* owned.c - a scoped task takes ownership of a moved value.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "task.h"

static int frees;
static int seen;

static void
take (own char *p)
{
  seen = p[0];
  release (p);
  frees++;
}

int
main (void)
{
  own char *buf = malloc (4);

  buf[0] = 'S';
  scope {
    go take (move (buf));
  }
  crug_task_runtime_shutdown ();

  printf ("scope-owned: seen=%c frees=%d\n", seen, frees);
  return (seen == 'S' && frees == 1) ? 0 : 1;
}
