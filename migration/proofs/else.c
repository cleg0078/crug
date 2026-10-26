/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: the release is the else branch of an "if".  The last
   non-blank line before free (p) is "else", not a control keyword, so a
   last-line heuristic misses the conditional path. */

#include <stdlib.h>

int
main (void)
{
  int c = 0;
  int *p = malloc (4);
  if (c)
    p[0] = 1;
  else
    free (p);
  return 0;
}
