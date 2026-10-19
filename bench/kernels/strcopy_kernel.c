/* Buffer copy kernel: d[i] = s[i].
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include "../kernel.h"

#define N 16384
#define REPS 512

#if BENCH_ARM == 2
void
bench_strcopy (mut char *d, ref const char *s)
#elif BENCH_ARM == 1
void
bench_strcopy (char *restrict d, const char *restrict s)
#else
void
bench_strcopy (char *d, const char *s)
#endif
{
  for (int r = 0; r < REPS; r++)
    for (int i = 0; i < N; i++)
      d[i] = (char) (s[i] + (char) r);
}
