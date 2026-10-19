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
/* { dg-options "-O2 -fcrug-result" } */

/* 'enum Name' still names the payload enum, at file scope and at block
   scope.  */

enum Message
{
  Text (char *),
  Quit
};

static enum Message g;

static void
set_global (void)
{
  g = Text ("x");
}

int
main (void)
{
  enum Message local;

  set_global ();
  local = Quit;
  if ((int) sizeof (enum Message) == 0)
    return 1;
  match (g)
    {
    Text (s) => { if (s[0] != 'x') return 2; }
    Quit => { return 3; }
    }
  match (local)
    {
    Text (s) => { return 4; }
    Quit => { }
    }
  return 0;
}
