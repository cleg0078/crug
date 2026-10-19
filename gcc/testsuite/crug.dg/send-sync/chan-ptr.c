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

/* A channel of unmanaged pointers is rejected: the element type must be
   Send (ADR 0026 D4).  */

/* { dg-prune-output "crug-CONC-002" } */

#include <stdlib.h>

int
main (void)
{
  chan char *c = channel (1);	/* { dg-error "crug-CONC-027" } */

  (void) c;
  return 0;
}
