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
   <http://www.gnu.org/licenses/>.  *//* { dg-do compile } */
/* { dg-options "-fcrug-channels" } */

/* The four arm forms: receive with a binding, receive without one, send,
   and default (ADR 0025 D3).  */

static int seen;

static void
pick (chan int in, chan int out)
{
  select {
    case v = recv (in):
      seen += v;
    case recv (in):
      seen += 1;
    case send (out, 7):
      seen += 2;
    default:
      seen += 3;
  }
}

int
main (void)
{
  chan int a = channel (1);
  chan int b = channel (1);

  pick (a, b);
  close (a);
  close (b);
  return 0;
}
