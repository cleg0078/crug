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
/* { dg-options "-fcrug-goroutines -fcrug-ownership -fcrug-borrow" } */

#include <stdlib.h>

/* The lender is declared in a block that ends before the scope does, so the
   child could still read it after it died.  */

static void
bump (mut int *p)
{
  *p += 1;
}

int
main (void)
{
  scope {
    if (1)
      {
	own int *n = adopt ((int *) malloc (sizeof (int)), free);

	*n = 0;
	go bump (borrow_mut (n));	/* { dg-error "crug-CONC-016" } */
	drop (n);
      }
  }
  return 0;
}
