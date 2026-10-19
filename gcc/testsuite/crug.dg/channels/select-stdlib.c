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
   <http://www.gnu.org/licenses/>.  *//* { dg-do compile } */
/* { dg-options "-fcrug-channels" } */

/* <stdlib.h> declares POSIX select on glibc.  'select {' still begins the
   select statement: no C statement can put a block after an identifier, so
   ADR 0002 D4 leaves the block form to Crug (a variable named select is
   still readable and assignable).  */

#include <stdlib.h>

static void
drain (chan int in)
{
  select {
    case v = recv (in):
      (void) v;
    default:
      return;
  }
}

int
main (void)
{
  chan int c = channel (1);

  drain (c);
  close (c);
  return 0;
}
