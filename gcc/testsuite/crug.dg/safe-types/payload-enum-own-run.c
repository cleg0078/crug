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

/* A payload enum that carries an owning payload is itself owned: the value is
   destroyed at the end of its scope, and the destructor runs only when the
   active variant is the owning one (ADR 0018 D8 R4, ADR 0004 D4).  */

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
owns_text (void)
{
  enum Msg m = Text (adopt (mk_buf (), destroy_buf));
  (void) m;
}

static void
owns_number (void)
{
  enum Msg m = Number (7);
  (void) m;
}

static void
owns_quit (void)
{
  enum Msg m = Quit;
  (void) m;
}

int
main (void)
{
  owns_text ();
  if (drop_count != 1)
    return 1;
  owns_number ();
  owns_quit ();
  if (drop_count != 1)
    return 2;
  return 0;
}
