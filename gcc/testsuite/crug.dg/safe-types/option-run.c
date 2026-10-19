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
/* { dg-options "-O2 -fcrug-result" } */

/* option<T> is a tag plus a value; match reads the tag and binds the payload
   of the selected variant only.  */

static option<int>
positives (int x)
{
  if (x > 0)
    return some (x);
  return none;
}

int
main (void)
{
  int a = 0;
  int b = 0;
  int n = 0;

  match (positives (5))
    {
    some (v) => { a = v; }
    none => { n = 1; }
    }
  match (positives (-2))
    {
    some (v) => { b = v; }
    none => { n = n + 2; }
    }
  if (a != 5 || b != 0 || n != 2)
    return 1;
  return 0;
}
