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

/* The operation is type-directed (ADR 0018 D6 R1): a declared close keeps
   its C meaning when the argument is not a channel, and the Crug operation
   applies when it is.  libc's close (2) and a channel close coexist in one
   translation unit.  */

#include <unistd.h>

int
main (void)
{
  chan int c = channel (1);

  close (1);
  close (c);
  return 0;
}
