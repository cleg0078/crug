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

/* Every way out of a scope waits for the group first: the normal end of the
   block, a return, a break and a goto out of the block.  */

static void
work (void)
{
}

static int
early (int stop)
{
  scope {
    go work ();
    if (stop)
      return 1;
  }
  return 0;
}

int
main (void)
{
  int i = 0;

  scope {
    go work ();
    if (i == 0)
      {
	i = 1;
	goto out;
      }
  }
 out:
  for (int n = 0; n < 2; n++)
    {
      scope {
	go work ();
	break;
      }
    }
  return early (1);
}
