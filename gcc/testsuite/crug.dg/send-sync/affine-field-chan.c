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
/* { dg-options "-fcrug-channels -fcrug-ffi=$srcdir/crug.dg/send-sync/affine-field.crugffi" } */

/* The same taint follows the object along the channel crossing: an aggregate
   that received an affine value may not be sent (fix-p3, CONC-02).  */

extern int render_slot (void);

struct Slot
{
  int value;
};

int
main (void)
{
  chan struct Slot c = channel (1);
  struct Slot s = { render_slot () };

  send (c, s);			/* { dg-error "crug-CONC-025" } */
  return 0;
}
