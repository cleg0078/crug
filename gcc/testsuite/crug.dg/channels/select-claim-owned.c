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
/* { dg-options "-fcrug-channels -fcrug-ownership" } */

/* A send arm of an owning channel claims the value through the owned claim
   entry point, so the ownership analysis recognises the move of the payload
   (ADR 0025 D3, D5).  The claim runs inside the rotation guard and the pass
   loop, and the analysis joins the retry path with the claimed path; a
   parameter moved by a select arm is therefore reported as possibly moved,
   the same report an 'if' around the send produces.  The report is on the
   parameter, not on the arm: it is the conservative half of the analysis,
   not a diagnosis of this statement.  */

#include <stdlib.h>

static void
hand_off (chan own char *out, own char *value)	/* { dg-warning "crug-OWN-007" } */
{
  select {
    case send (out, move (value)):
      ;
  }
}

int
main (void)
{
  chan own char *out = channel (1);
  own char *p = malloc (4);

  hand_off (out, p);
  close (out);
  return 0;
}
