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
/* { dg-options "-O2 -fcrug-borrow" } */

/* Without -fcrug-alias-facts nothing changes: the borrow checker is on, but a
   plain pointer is still accepted where a mut parameter exists (the historical
   leniency of docs/crug/BORROW.md section 2.4), because no restrict fact is
   claimed for it.  */

static void
set (mut int *p, int v)
{
  *p = v;
}

int
main (void)
{
  int x = 0;
  set (&x, 7);
  return x == 7 ? 0 : 1;
}
