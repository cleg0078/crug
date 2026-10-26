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
/* { dg-options "-fcrug-channels" } */

/* Each arm of a select is claimed once per pass (ADR 0025 D5): a receive
   claims into a temporary and binds it only when the claim succeeded, a send
   claims the value before the arm is entered, and no arm is probed and then
   performed.  The rotation makes every pass start with another arm.  */

static int seen;

static void
pick (chan int in, chan int spare, chan int out)
{
  for (int round = 0; round < 8; round++)
    select {
      case v = recv (in):
	seen += v;
	break;
      case recv (spare):
	seen += 1;
	break;
      case send (out, round):
	seen += 2;
	break;
      default:
	seen += 3;
    }
}

int
main (void)
{
  chan int a = channel (1);
  chan int b = channel (1);
  chan int c = channel (1);

  pick (a, b, c);
  close (a);
  close (b);
  close (c);
  return seen;
}
