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
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

/* The contextual-qualifier scan must be able to read 8 tokens ahead: 7
   chained qualifiers and the declaration specifiers that follow them.  Before
   the look-ahead buffer was enlarged, the 8th call to
   c_parser_peek_nth_token wrote past the four-token buffer and corrupted the
   parser state (P3-008).  The chained qualifiers are still rejected, one
   crug-TYPE-002 for each qualifier after the first, and the parser recovers
   to the declarator that follows.  */

own
ref			/* { dg-error "crug-TYPE-002" } */
mut			/* { dg-error "crug-TYPE-002" } */
raw			/* { dg-error "crug-TYPE-002" } */
own			/* { dg-error "crug-TYPE-002" } */
ref			/* { dg-error "crug-TYPE-002" } */
mut			/* { dg-error "crug-TYPE-002" } */ int *g;
