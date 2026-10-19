/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample with ambiguous ownership: the release only runs on one
   branch, so c2crug reports raw instead of inventing an owned value. */

#include <stdio.h>
#include <stdlib.h>

int
main (void)
{
  int *scores = malloc (4 * sizeof (int));

  scores[0] = 10;
  scores[1] = 20;
  scores[2] = 30;
  scores[3] = 40;

  printf ("04-branch %d\n", scores[0] + scores[3]);

  if (scores[0] > 0)
    free (scores);

  return 0;
}
