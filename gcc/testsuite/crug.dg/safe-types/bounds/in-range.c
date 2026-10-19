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

/* The bounds check must not change a defined program: every index is in
   range, and the sum is the sum of the elements.  */

int
main (void)
{
  int a[4] = { 1, 2, 3, 4 };
  slice<int> s = slice (a, 4);
  int total = 0;
  int i;

  for (i = 0; i < 4; i++)
    total = total + s[i];
  return total == 10 ? 0 : 1;
}
