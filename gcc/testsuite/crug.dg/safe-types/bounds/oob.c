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

/* An index at or past the length is a trap in checked mode (BUILT_IN_TRAP,
   so the process dies on SIGILL rather than reading out of bounds), and
   -fno-crug-bounds-checks removes the trap.  scripts/check-safe-bounds.sh
   runs this program and inspects the object, so it is not a dg-test.  */

int
main (void)
{
  int a[2] = { 1, 2 };
  slice<int> s = slice (a, 2);
  return s[5];
}
