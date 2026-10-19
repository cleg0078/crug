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
/* { dg-options "-O2 -fcrug-goroutines" } */

/* 'scope:' stays a label; only 'scope {' is a scope statement.  */

int
main (void)
{
  int n = 0;

 scope:
  n++;
  if (n < 2)
    goto scope;
  return n == 2 ? 0 : 1;
}
