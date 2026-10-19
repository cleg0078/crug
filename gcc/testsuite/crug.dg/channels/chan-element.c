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

/* The element must have a size known at compile time (ADR 0025 D1); the
   declaration is then already in error, so 'channel' also reports that it
   has no type from context.  */
/* { dg-prune-output "crug-CONC-002" } */

int
main (void)
{
  chan void bad = channel (1);	/* { dg-error "crug-CONC-019" } */
  return 0;
}
