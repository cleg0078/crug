/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: one null guard whose then branch returns, the guard
   dominates the single release and the pointer is not reassigned between.
   c2crug may migrate it, but only with the guard-level confidence 0.80. */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  int *p = malloc (sizeof (int));
  if (!p)
    return 1;
  p[0] = 7;
  printf ("proofs: valid guard %d\n", p[0]);
  free (p);
  return 0;
}
