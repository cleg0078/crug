/* Stencil kernel: d[i] = a[i-1] + 2*a[i] + a[i+1].
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include "../kernel.h"

#define N 8192
#define REPS 512

#if BENCH_ARM == 2
void
bench_stencil (mut int *d, ref const int *a)
#elif BENCH_ARM == 1
void
bench_stencil (int *restrict d, const int *restrict a)
#else
void
bench_stencil (int *d, const int *a)
#endif
{
  for (int r = 0; r < REPS; r++)
    for (int i = 1; i < N - 1; i++)
      d[i] += a[i - 1] + 2 * a[i] + a[i + 1];
}
