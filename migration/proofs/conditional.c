/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: the release is the unbraced body of an if.  It is the
   ordinary conditional path control for the labelled and else variants. */

#include <stdlib.h>

int
main (void)
{
  int c = 1;
  int *p = malloc (4);
  if (c)
    free (p);
  return 0;
}
