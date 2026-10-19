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
/* { dg-do run } */
/* { dg-options "-O2 -fcrug-adt -fcrug-ownership" } */

/* Moving an owning payload enum into match transfers the payload to the arm
   binding, which destroys it at the end of the arm; the moved scrutinee is
   never destroyed again.  A '_' arm may cover the variants that own
   nothing.  */

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

static int
bound (void)
{
  enum Msg m = Text (adopt (mk_buf (), destroy_buf));
  int seen = 0;

  match (move (m))
    {
    Text (v) => { seen = (v != 0); }
    Number (n) => { seen = n; }
    Quit => { }
    }
  return seen;
}

static int
wildcard (void)
{
  enum Msg m = Text (adopt (mk_buf (), destroy_buf));
  int seen = 0;

  match (move (m))
    {
    Text (v) => { seen = (v != 0); }
    _ => { seen = 100; }
    }
  return seen;
}

int
main (void)
{
  if (bound () != 1)
    return 1;
  if (drop_count != 1)
    return 2;
  if (wildcard () != 1)
    return 3;
  if (drop_count != 2)
    return 4;
  return 0;
}
