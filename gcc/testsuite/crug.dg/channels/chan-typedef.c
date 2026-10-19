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

/* A channel type is an ordinary type, so a typedef keeps working.  */

typedef chan int int_chan;

static int
take (int_chan a)
{
  int v = recv (a);

  close (a);
  return v;
}

int
main (void)
{
  int_chan a = channel (1);

  send (a, 5);
  return take (a) == 5 ? 0 : 1;
}
