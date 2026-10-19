/* Vector add kernel: d[i] = a[i] + b[i].
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   BENCH_ARM 0  ordinary C pointers
   BENCH_ARM 1  C pointers declared restrict by hand
   BENCH_ARM 2  Crug mut/ref parameters; -fcrug-alias-facts gives the exclusive
                destination the same noalias fact a hand-written restrict has  */

#include "../kernel.h"

#define N 8192
#define REPS 512

#if BENCH_ARM == 2
void
bench_vadd (mut int *d, ref const int *a, ref const int *b)
#elif BENCH_ARM == 1
void
bench_vadd (int *restrict d, const int *restrict a, const int *restrict b)
#else
void
bench_vadd (int *d, const int *a, const int *b)
#endif
{
  for (int r = 0; r < REPS; r++)
    for (int i = 0; i < N; i++)
      d[i] += a[i] + b[i];
}
