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

/* 'go f (args);' copies the arguments into a frame and hands the entry point
   to the runtime; the call itself does not run here.  */

static int values[8];

static void
store (int i, int v)
{
  values[i] = v;
}

int
main (void)
{
  for (int i = 0; i < 8; i++)
    go store (i, i + 1);
  return 0;
}
