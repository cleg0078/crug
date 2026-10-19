/* Buffer copy driver.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "../harness.h"
#include "../kernel.h"

#define N 16384

int
main (void)
{
  char *s = malloc (N);
  char *d = malloc (N);

  for (int i = 0; i < N; i++)
    {
      s[i] = (char) (i * 7 + 3);
      d[i] = 0;
    }

  long long ns;
  BENCH_RUN (bench_strcopy (d, s), ns);

  long long checksum = 0;
  for (int i = 0; i < N; i++)
    checksum += d[i];

  bench_report ("strcopy", ns, checksum);
  free (s);
  free (d);
  return 0;
}
