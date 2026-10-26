/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: the release is the body of a switch case.  The brace depth
   alone cannot prove it unconditional, because the case entry is chosen by
   the switch expression. */

#include <stdlib.h>

int
main (void)
{
  int c = 1;
  int *p = malloc (4);
  switch (c)
    {
    case 1:
      free (p);
      break;
    default:
      break;
    }
  return 0;
}
