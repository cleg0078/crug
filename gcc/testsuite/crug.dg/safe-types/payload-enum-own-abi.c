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
/* { dg-options "-O2 -fcrug-adt -fcrug-ownership" } */

/* An owning payload is recorded in the analysis only.  It is erased from the
   type, so an owning payload enum has exactly the layout of the same enum
   with a non-owning payload (see docs/crug/ABI.md section 3).  */

#include <stddef.h>

struct Buf { int x; };

enum Plain
{
  PlainText (struct Buf *),
  PlainNumber (int),
  PlainQuit
};

enum Owned
{
  OwnedText (own struct Buf *),
  OwnedNumber (int),
  OwnedQuit
};

_Static_assert (sizeof (enum Plain) == sizeof (enum Owned), "layout");
_Static_assert (_Alignof (enum Plain) == _Alignof (enum Owned), "alignment");
_Static_assert (offsetof (enum Plain, u) == offsetof (enum Owned, u), "union");
