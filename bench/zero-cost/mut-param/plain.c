/* Plain-C twin of mut-param/crug.c.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

void
mut_fill (int *d, long n)
{
  for (long i = 0; i < n; i++)
    d[i] = (int) i;
}
