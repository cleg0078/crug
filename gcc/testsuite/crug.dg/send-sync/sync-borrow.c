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
/* { dg-options "-fcrug-goroutines -fcrug-borrow" } */

/* A scoped task may borrow a Send or Sync pointee: a mutable borrow needs
   Send, a shared borrow needs Sync (ADR 0026 D3).  */

#include <stdlib.h>

static void
bump (mut int *p)
{
  *p = *p + 1;
}

static void
look (ref int *p)
{
  (void) p;
}

int
main (void)
{
  own int *n = malloc (sizeof (int));
  own int *m = malloc (sizeof (int));

  scope
  {
    go bump (borrow_mut (n));
  }
  scope
  {
    go look (borrow (m));
  }
  return 0;
}
