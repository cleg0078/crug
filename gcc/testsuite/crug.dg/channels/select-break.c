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

/* A top-level select is not a loop or a switch, so a 'break' written in an
   arm follows the ordinary C rule and is rejected while parsing.  The
   lowering's own 'break' leaves the generated loop and is not source-level
   (fix-p3, CONC-03).  */

static void
pick (chan int in)
{
  select {
    case recv (in):
      break;	/* { dg-error "not within loop or switch" } */
    default:
      break;	/* { dg-error "not within loop or switch" } */
  }
}
