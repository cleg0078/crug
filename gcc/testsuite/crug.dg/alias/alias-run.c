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
/* { dg-do run } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts" } */

/* A mut parameter is an exclusive borrow: the call site passes borrow_mut,
   the callee writes through it and reads a shared borrow.  The object code is
   checked against the hand-written restrict twin by
   scripts/check-optimization.sh; this test checks that the program still
   computes the right values with the fact enabled.  */

#include <stdlib.h>

static void
add_into (mut int *d, ref const int *a, int n)
{
  for (int i = 0; i < n; i++)
    d[i] = a[i] + 1;
}

int
main (void)
{
  own int *a = adopt (malloc (4 * sizeof (int)), free);
  own int *d = adopt (malloc (4 * sizeof (int)), free);

  for (int i = 0; i < 4; i++)
    {
      a[i] = i;
      d[i] = 0;
    }

  add_into (borrow_mut (d), borrow (a), 4);

  if (d[0] != 1 || d[3] != 4)
    return 1;
  return 0;
}
