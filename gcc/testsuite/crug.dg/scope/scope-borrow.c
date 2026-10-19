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

/* A scoped task may capture a borrow of a value that outlives the scope
   block: the block waits for the child, so the loan is still alive while the
   child can use it.  */

static void
bump (mut int *p)
{
  *p += 1;
}

int
main (void)
{
  own int *n = adopt ((int *) malloc (sizeof (int)), free);

  *n = 0;
  scope {
    go bump (borrow_mut (n));
  }
  drop (n);
  return 0;
}
