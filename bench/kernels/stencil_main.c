/* Stencil driver.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "../harness.h"
#include "../kernel.h"

#define N 8192

int
main (void)
{
  int *a = malloc (N * sizeof (int));
  int *d = malloc (N * sizeof (int));

  for (int i = 0; i < N; i++)
    {
      a[i] = i & 15;
      d[i] = 0;
    }

  long long ns;
  BENCH_RUN (bench_stencil (d, a), ns);

  long long checksum = 0;
  for (int i = 0; i < N; i++)
    checksum += d[i];

  bench_report ("stencil", ns, checksum);
  free (a);
  free (d);
  return 0;
}
