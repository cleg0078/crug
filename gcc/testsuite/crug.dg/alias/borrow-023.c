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

/* The lender is passed as another argument of the same call, so the callee's
   mut parameter is not the only view of the object.  */

#include <stdlib.h>

static void
copy (mut int *d, int *p, int n)
{
  for (int i = 0; i < n; i++)
    d[i] = p[i];
}

int
main (void)
{
  own int *x = adopt (malloc (4 * sizeof (int)), free);
  copy (borrow_mut (x), x, 4); /* { dg-error "crug-BORROW-023" } */
  return 0;
}
