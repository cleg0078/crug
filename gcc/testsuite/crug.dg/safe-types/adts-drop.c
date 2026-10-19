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

/* Gate evidence for the owning payload-enum row: when the active variant is
   not known statically, the destructor call is guarded by the tag, so it runs
   for the owning variant and never for an inactive one.  Run with
   -fcrug-dump=drop this file reports one drop, for the owning variant, and
   none for a value that is statically a non-owning variant
   (scripts/check-owned-payloads.sh).  */

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
picked (int want_text)
{
  enum Msg maybe;

  if (want_text)
    maybe = Text (adopt (mk_buf (), destroy_buf));
  else
    maybe = Number (42);
  (void) maybe;
  return 0;
}

int
main (void)
{
  enum Msg inactive = Number (7);

  (void) inactive;
  picked (0);
  if (drop_count != 0)
    return 1;
  picked (1);
  if (drop_count != 1)
    return 2;
  return 0;
}
