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
/* { dg-options "-fcrug-goroutines" } */

/* 'scope { ... }' is a task group: the block waits for every child before it
   ends.  A scoped spawn passes the group handle to the runtime.  */

static int values[4];

static void
store (int i)
{
  values[i] = i;
}

int
main (void)
{
  scope {
    for (int i = 0; i < 4; i++)
      go store (i);
  }
  return values[3] == 3 ? 0 : 1;
}
