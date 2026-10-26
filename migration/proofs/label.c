/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture for R4-TOOL-001: the release is the body of an "if" and the
   last line before it is a label.  A goto can target the label, so the
   release is conditional and c2crug must leave the pointer raw. */

#include <stdlib.h>

int
main (void)
{
  int c = 1;
  int *p = malloc (4);
  if (c)
  label:
    free (p);
  return 0;
}
