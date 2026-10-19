/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample with an alias: the allocation escapes into a second
   pointer, so ownership stays ambiguous and is reported as raw. */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  int *first = malloc (2 * sizeof (int));
  int *second = first;

  first[0] = 7;
  first[1] = 9;

  printf ("06-alias %d\n", second[0] + second[1]);

  free (first);
  return 0;
}
