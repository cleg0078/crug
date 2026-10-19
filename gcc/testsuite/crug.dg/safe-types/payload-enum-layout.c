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
/* { dg-options "-fcrug-result" } */

/* The payload enum layout recorded in docs/crug/ABI.md: an unsigned int tag
   at offset 0 and the payload union after it, aligned as the union requires.
   The inactive payload bytes are zero; there is no niche representation.  */

#include <stddef.h>

enum Message
{
  Text (char *),
  Image (char *),
  Quit
};

typedef enum Message message;

_Static_assert (sizeof (message) == 16, "payload enum size");
_Static_assert (_Alignof (message) == _Alignof (void *), "payload enum align");
_Static_assert (offsetof (message, tag) == 0, "payload enum tag offset");
_Static_assert (offsetof (message, u) == 8, "payload enum union offset");
_Static_assert (offsetof (message, u.Text) == 8, "Text member offset");
_Static_assert (offsetof (message, u.Image) == 8, "Image member offset");
