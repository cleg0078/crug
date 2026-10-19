/* Copyright (C) 2026 CLEG0078

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3, or (at your option) any later
   version.

   GCC is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */
/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts" } */

/* Two borrows of one lender in a single call: one mutable, one shared.  The
   callee's mut parameter is restrict-qualified, so this is rejected before it
   can become a false noalias fact.  */

#include <stdlib.h>

static void
copy (mut int *d, ref const int *a, int n)
{
  for (int i = 0; i < n; i++)
    d[i] = a[i];
}

int
main (void)
{
  own int *x = adopt (malloc (4 * sizeof (int)), free);
  copy (borrow_mut (x), borrow (x), 4); /* { dg-error "crug-BORROW-002" } */
  return 0;
}
