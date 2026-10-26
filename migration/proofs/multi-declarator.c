/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Proof fixture: the declaration introduces two pointers and each one has
   its own initializer.  c2crug must not read the comma as part of the first
   initializer; the line stays raw as multiple declarators. */

#include <stdlib.h>

int
main (void)
{
  char *p = malloc (4), *q = malloc (4);
  free (p);
  free (q);
  return 0;
}
