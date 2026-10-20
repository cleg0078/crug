/* Plain-C twin of ref-param/crug.c.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

long
ref_sum (const int *a, long n)
{
  long s = 0;
  for (long i = 0; i < n; i++)
    s += a[i];
  return s;
}
