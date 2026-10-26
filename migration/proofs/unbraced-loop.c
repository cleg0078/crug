/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: the release is the brace-less body of a for loop, so it can
   run zero or many times and is never an unconditional single release. */

#include <stdlib.h>

int
main (void)
{
  int *p = malloc (4);
  for (int i = 0; i < 1; i++)
    free (p);
  return 0;
}
