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

/* Two mutable borrows of the same lender in one call.  */

#include <stdlib.h>

static void
twice (mut int *d, mut int *e)
{
  *d = 1;
  *e = 2;
}

int
main (void)
{
  own int *x = adopt (malloc (sizeof (int)), free);
  twice (borrow_mut (x), borrow_mut (x)); /* { dg-error "crug-BORROW-003" } */
  return 0;
}
