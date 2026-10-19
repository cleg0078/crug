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

/* result<T, E> is a tag plus a union, so the ok and err payloads overlap and
   only the selected one is read.  Nested matches cover result<option<T>, E>,
   which is the shape of a fallible lookup.  */

static result<int, int>
divide (int a, int b)
{
  if (b == 0)
    return err (7);
  return ok (a / b);
}

static result<option<int>, int>
lookup (int key)
{
  if (key == 0)
    return ok (none);
  if (key < 0)
    return err (9);
  return ok (some (key * 2));
}

int
main (void)
{
  int got = 0;
  int bad = 0;

  match (divide (10, 2))
    {
    ok (v) => { got = v; }
    err (e) => { bad = e; }
    }
  if (got != 5 || bad != 0)
    return 1;

  match (divide (1, 0))
    {
    ok (v) => { bad = bad + v; }
    err (e) => { got = e; }
    }
  if (got != 7 || bad != 0)
    return 2;

  match (lookup (3))
    {
    ok (o) => {
      match (o)
	{
	some (v) => { got = v; }
	none => { bad = 1; }
	}
    }
    err (e) => { bad = e; }
    }
  if (got != 6 || bad != 0)
    return 3;

  match (lookup (0))
    {
    ok (o) => {
      match (o)
	{
	some (v) => { bad = v; }
	none => { got = 11; }
	}
    }
    err (e) => { bad = e; }
    }
  if (got != 11 || bad != 0)
    return 4;

  return 0;
}
