/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* -fcrug-alias-facts: a mut parameter is an exclusive borrow, so it is
   compiled as 'restrict'.  With -fopt-info-vec the loop below is reported as
   vectorized with the flag and not without it (docs/crug/OPTIMIZATION.md).

   Build and run:
     crugc -O2 -fcrug-borrow -fcrug-alias-facts examples/15-alias-facts.c -o ex15
     ./ex15                     -> total=72

   The caller must prove exclusivity too: `borrow_mut (d)` for the mut
   parameter, `borrow (a)` for the shared one.  A plain pointer at `scale`
   would be rejected with crug-BORROW-021.  The dump records the fact:
     crugc -O2 -fcrug-borrow -fcrug-alias-facts -fcrug-dump=alias ...
     cat 15-alias-facts.crug-alias
                                -> alias scale: parameter d restrict (mut)  */

#include <stdio.h>
#include <stdlib.h>

static void
destroy_floats (float *p)
{
  free (p);
}

static void
scale (mut float *d, ref const float *a, unsigned long n)
{
  for (unsigned long i = 0; i < n; i++)
    d[i] = a[i] * 2.0f;
}

int
main (void)
{
  unsigned long n = 8;
  own float *a = adopt (malloc (n * sizeof (float)), destroy_floats);
  own float *d = adopt (malloc (n * sizeof (float)), destroy_floats);

  for (unsigned long i = 0; i < n; i++)
    a[i] = (float) i + 1.0f;

  scale (borrow_mut (d), borrow (a), n);

  float total = 0;
  for (unsigned long i = 0; i < n; i++)
    total += d[i];
  printf ("total=%d\n", (int) total);
  return total == 72.0f ? 0 : 1;
}
