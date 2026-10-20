/* Zero-cost case: a shared borrow parameter (ref) in a loop.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

long
ref_sum (ref const int *a, long n)
{
  long s = 0;
  for (long i = 0; i < n; i++)
    s += a[i];
  return s;
}
