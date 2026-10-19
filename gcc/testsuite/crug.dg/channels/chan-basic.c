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

/* A buffered channel: creation, transfer of an integer element by value,
   receive, and close (ADR 0025 D2).  The run-time behaviour is exercised by
   libcrug/tests/channels and scripts/check-channels.sh.  */

static void
round_trip (void)
{
  chan int a = channel (4);
  int v;

  send (a, 7);
  v = recv (a);
  close (a);
  (void) v;
}

int
main (void)
{
  round_trip ();
  return 0;
}
