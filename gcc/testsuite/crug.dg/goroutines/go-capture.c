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

/* Arguments are captured by value: no arguments and a struct work.  An
   array decays to a pointer, which is not Send, so that capture is rejected
   (ADR 0026 D2).  */

struct Pair
{
  int a;
  int b;
};

static void
tick (void)
{
}

static void
fill (int *p, int n)
{
  for (int i = 0; i < n; i++)
    p[i] = i;
}

static void
use_pair (struct Pair p)
{
  (void) p;
}

int
main (void)
{
  int buf[4];
  struct Pair p = { 7, 9 };

  go tick ();
  go fill (buf, 4);		/* { dg-error "crug-CONC-024" } */
  go use_pair (p);
  return buf[0];
}
