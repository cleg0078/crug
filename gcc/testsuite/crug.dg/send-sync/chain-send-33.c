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

/* A value nested 33 levels deep is past the inference limit; it is rejected
   rather than assumed Send (fix-p3, CONC-01).  */

struct l1
{
  int x;
};

struct l2
{
  struct l1 v;
};

struct l3
{
  struct l2 v;
};

struct l4
{
  struct l3 v;
};

struct l5
{
  struct l4 v;
};

struct l6
{
  struct l5 v;
};

struct l7
{
  struct l6 v;
};

struct l8
{
  struct l7 v;
};

struct l9
{
  struct l8 v;
};

struct l10
{
  struct l9 v;
};

struct l11
{
  struct l10 v;
};

struct l12
{
  struct l11 v;
};

struct l13
{
  struct l12 v;
};

struct l14
{
  struct l13 v;
};

struct l15
{
  struct l14 v;
};

struct l16
{
  struct l15 v;
};

struct l17
{
  struct l16 v;
};

struct l18
{
  struct l17 v;
};

struct l19
{
  struct l18 v;
};

struct l20
{
  struct l19 v;
};

struct l21
{
  struct l20 v;
};

struct l22
{
  struct l21 v;
};

struct l23
{
  struct l22 v;
};

struct l24
{
  struct l23 v;
};

struct l25
{
  struct l24 v;
};

struct l26
{
  struct l25 v;
};

struct l27
{
  struct l26 v;
};

struct l28
{
  struct l27 v;
};

struct l29
{
  struct l28 v;
};

struct l30
{
  struct l29 v;
};

struct l31
{
  struct l30 v;
};

struct l32
{
  struct l31 v;
};

struct l33
{
  struct l32 v;
};

static void
use (struct l33 v)
{
  (void) v;
}

int
main (void)
{
  struct l33 v;

  go use (v);		/* { dg-error "crug-CONC-024" } */
  return 0;
}
