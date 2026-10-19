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

/* An unmanaged pointer names an object without saying who may touch it, so
   it is not Send and the capture is rejected (ADR 0026 D2); the documented
   escape is the FFI 'send' annotation, never a silent conversion.  */

static void
use (int *p)
{
  (void) p;
}

int
main (void)
{
  int x = 1;

  go use (&x);		/* { dg-error "crug-CONC-024" } */
  return 0;
}
