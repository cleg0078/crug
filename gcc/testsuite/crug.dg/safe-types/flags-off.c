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
/* { dg-options "" } */

/* Without -fcrug-slices or -fcrug-result the generic type constructors are
   ordinary identifiers: the hooks in the parser are feature-gated, so plain C
   sees a syntax error at the '<' and nothing else.  Declarations rather than
   statements are used so that no cascading errors follow the first one.  */

slice<int> g_slice;   /* { dg-error "before '<' token" } */
option<int> g_option; /* { dg-error "before '<' token" } */
