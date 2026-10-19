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
/* { dg-options "-fcrug-adt -fcrug-ownership" } */

/* The destructor of an owning payload enum is not part of its ABI, so the
   value cannot cross a function boundary by value.  */

#include <stdlib.h>

struct Buf { int x; };
static int drop_count;

static void
destroy_buf (struct Buf *b)
{
  drop_count++;
  free (b);
}

static struct Buf *
mk_buf (void)
{
  return malloc (sizeof (struct Buf));
}

enum Msg
{
  Text (own struct Buf *),
  Number (int),
  Quit
};

static void
take (enum Msg m) /* { dg-error "cannot be passed by value" } */
{
  (void) m;
}

int
main (void)
{
  enum Msg m = Number (1);
  (void) m;
  return 0;
}
