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

/* An unbuffered channel hands the value straight to the receiver, and a
   scope keeps both sides alive until the transfers are done.  */

static int total;

static void
producer (chan int out)
{
  for (int i = 0; i < 3; i++)
    send (out, i);
}

static void
consumer (chan int in)
{
  for (int i = 0; i < 3; i++)
    total += recv (in);
}

int
main (void)
{
  chan int ch = channel (0);

  scope {
    go producer (ch);
    go consumer (ch);
  }
  close (ch);
  return total == 3 ? 0 : 1;
}
