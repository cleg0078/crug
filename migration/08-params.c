/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample with pointer parameters: c2crug suggests ref/mut for
   them but never applies a suggestion.  Both parameters are plain C. */

#include <stdio.h>

static int
total (const int *values, int n)
{
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += values[i];
  return sum;
}

static void
fill (int *values, int n)
{
  for (int i = 0; i < n; i++)
    values[i] = i + 1;
}

int
main (void)
{
  int values[4];

  fill (values, 4);
  printf ("08-params %d\n", total (values, 4));
  return 0;
}
