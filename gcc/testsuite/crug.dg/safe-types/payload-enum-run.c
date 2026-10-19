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

/* A payload enum is a tag plus the payload of the selected variant (ADR 0018
   D8).  The constructor stores the tag and the payload; match reads the tag
   and binds the payload of the matching variant only.  */

#include <stddef.h>

enum Message
{
  Text (char *),
  Image (char *),
  Quit
};

static int
describe (enum Message m)
{
  int r = 0;

  match (m)
    {
    Text (s) => { r = 1 + (s[0] == 'h'); }
    Image (s) => { r = 10 + (s[0] == 'p'); }
    Quit => { r = 100; }
    }
  return r;
}

int
main (void)
{
  enum Message a = Text ("hello");
  enum Message b = Image ("pic");
  enum Message c = Quit;
  int t = 0;

  if (describe (a) != 2 || describe (b) != 11 || describe (c) != 100)
    return 1;

  /* The arms may come in any order and a '_' arm is the catch-all.  */
  match (a)
    {
    Quit => { t = 1; }
    Image (s) => { t = 2; }
    Text (s) => { t = 3; }
    }
  if (t != 3)
    return 2;

  match (b)
    {
    _ => { t = 4; }
    }
  if (t != 4)
    return 3;
  return 0;
}
