/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample: a heap buffer with one release on every path.
   The allocation is a catalogue function and the release is a single
   standalone statement, so c2crug can prove ownership and turn the free
   into drop. */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  int n = 8;
  int *values = malloc ((size_t) n * sizeof (int));

  for (int i = 0; i < n; i++)
    values[i] = i * i;

  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += values[i];

  printf ("01-heap %d\n", sum);

  free (values);
  return 0;
}
