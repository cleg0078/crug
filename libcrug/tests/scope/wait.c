/* wait.c - a scope waits for every child before the block ends.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdatomic.h>
#include <stdio.h>

#include "task.h"

static int order[8];
static _Atomic int next;

static void
record (int i)
{
  order[atomic_fetch_add_explicit (&next, 1, memory_order_relaxed)] = i;
}

int
main (void)
{
  int seen = 0;

  scope {
    for (int i = 0; i < 8; i++)
      go record (i);
  }

  /* Every child has finished by the time the block ends, so all eight
     results are visible here; nothing is still queued.  */
  for (int i = 0; i < 8; i++)
    if (order[i] >= 0 && order[i] < 8)
      seen++;

  printf ("scope-wait: children=%d slots=%d\n", next, seen);
  return (next == 8 && seen == 8) ? 0 : 1;
}
