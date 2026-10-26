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

/* The runtime copies a task frame with the alignment of max_align_t and
   rounds the size up to that alignment (libcrug/core/task.c spawn_common),
   so a capture that demands a stricter alignment cannot live in the frame:
   the thunk would read the copy through a wrong promise.  AUD-006,
   crug-CONC-028.  */

struct Wide
{
  int value;
} __attribute__ ((aligned (32)));

static void
take_wide (struct Wide w)
{
  (void) w;
}

int
main (void)
{
  struct Wide w = { 1 };

  go take_wide (w);		/* { dg-error "crug-CONC-028" } */
  return w.value;
}
