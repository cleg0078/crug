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
/* { dg-options "-O2 -fcrug-slices" } */

/* A slice is a pointer and a length; indexing it is a load, its length is a
   field, and a slice of an array decays.  The checks are runtime results, so
   the layout and the bounds comparison are exercised on the target.  */

#include <stddef.h>

static int
sum (slice<int> s)
{
  int total = 0;
  size_t i;

  for (i = 0; i < s.len; i++)
    total = total + s[i];
  return total;
}

static char
first (slice<char> s)
{
  return s.len > 0 ? s[0] : '?';
}

int
main (void)
{
  int a[4] = { 1, 2, 3, 4 };
  char b[3] = { 'a', 'b', 'c' };
  slice<int> s = slice (a, 4);
  slice<char> c = slice (b, 3);

  if (sum (s) != 10)
    return 1;
  if (sum (slice (a + 1, 2)) != 5)
    return 2;
  if (first (c) != 'a')
    return 3;
  if (c.len != 3)
    return 4;
  return 0;
}
