/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample with one proven and one ambiguous allocation in the same
   function: c2crug must transform only the proven one. */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  char *label = malloc (8);
  char *scratch = malloc (8);

  label[0] = 'o';
  label[1] = 'k';
  label[2] = 0;

  scratch[0] = 'x';
  scratch[1] = 0;

  printf ("07-mixed %s %s\n", label, scratch);

  if (label[0] == 'o')
    free (scratch);

  free (label);
  return 0;
}
