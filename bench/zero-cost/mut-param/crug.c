/* Zero-cost case: an exclusive borrow parameter (mut) in a loop.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

void
mut_fill (mut int *d, long n)
{
  for (long i = 0; i < n; i++)
    d[i] = (int) i;
}
