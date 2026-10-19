/* Vector add driver: allocate, warm up, time and report.
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
  int *b = malloc (N * sizeof (int));
  int *d = malloc (N * sizeof (int));

  for (int i = 0; i < N; i++)
    {
      a[i] = i & 3;
      b[i] = (i & 7) + 1;
      d[i] = 0;
    }

  long long ns;
  BENCH_RUN (bench_vadd (d, a, b), ns);

  long long checksum = 0;
  for (int i = 0; i < N; i++)
    checksum += d[i];

  bench_report ("vadd", ns, checksum);
  free (a);
  free (b);
  free (d);
  return 0;
}
