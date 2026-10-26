/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture for R4-TOOL-002: the first early exit is a real null guard,
   but the second return is not covered by that guard.  The allocation can be
   skipped on a path the guard does not describe, so the pointer stays raw. */

#include <stdlib.h>

int
main (void)
{
  int c = 0;
  int *p = malloc (4);
  if (!p)
    return 1;
  if (c)
    return 2;
  free (p);
  return 0;
}
