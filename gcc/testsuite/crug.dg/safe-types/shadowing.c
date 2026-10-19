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

/* A visible declaration wins over a contextual word (ADR 0018 D7), so a C
   program that already has a function called 'some' keeps calling it.  */

static int
some (int x)
{
  return x + 1;
}

int
main (void)
{
  return some (41) == 42 ? 0 : 1;
}
