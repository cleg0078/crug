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
/* { dg-options "-fcrug-slices -fcrug-result" } */

/* The layouts recorded in docs/crug/ABI.md, asserted here.  The gate check
   compiles this file, so a layout change is a gate failure, not a surprise.

   Field names are part of the record: slice<T> is { T *ptr; size_t len; },
   option<T> is { _Bool some; T value; } and result<T, E> is
   { _Bool ok; union { T value; E error; } u; }.  A type with a comma in its
   argument list cannot appear in a macro argument, so each one is typedef'd
   first: the preprocessor sees the comma.  */

#include <stddef.h>

typedef slice<int> slice_int;
typedef slice<char> slice_char;
typedef option<int> option_int;
typedef option<char> option_char;
typedef option<long> option_long;
typedef result<int, int> result_int_int;
typedef result<char, long> result_char_long;

_Static_assert (sizeof (slice_int) == 2 * sizeof (void *), "slice size");
_Static_assert (_Alignof (slice_int) == _Alignof (void *), "slice align");
_Static_assert (offsetof (slice_int, ptr) == 0, "slice ptr offset");
_Static_assert (offsetof (slice_int, len) == sizeof (void *), "slice len offset");
_Static_assert (sizeof (slice_char) == 2 * sizeof (void *), "slice<char> size");
_Static_assert (_Alignof (slice_char) == _Alignof (void *), "slice<char> align");

_Static_assert (sizeof (option_int) == 2 * sizeof (int), "option<int> size");
_Static_assert (_Alignof (option_int) == _Alignof (int), "option<int> align");
_Static_assert (offsetof (option_int, some) == 0, "option tag offset");
_Static_assert (offsetof (option_int, value) == sizeof (int), "option value offset");
_Static_assert (sizeof (option_char) == 2, "option<char> size");
_Static_assert (_Alignof (option_char) == 1, "option<char> align");
_Static_assert (sizeof (option_long) == 2 * sizeof (long), "option<long> size");
_Static_assert (offsetof (option_long, value) == sizeof (long),
		"option<long> value offset");

_Static_assert (sizeof (result_int_int) == 2 * sizeof (int), "result size");
_Static_assert (_Alignof (result_int_int) == _Alignof (int), "result align");
_Static_assert (offsetof (result_int_int, ok) == 0, "result tag offset");
_Static_assert (offsetof (result_int_int, u) == sizeof (int), "result union offset");
_Static_assert (offsetof (result_int_int, u.value) == sizeof (int),
		"result ok member offset");
_Static_assert (offsetof (result_int_int, u.error) == sizeof (int),
		"result err member offset");
_Static_assert (sizeof (result_char_long) == 2 * sizeof (long),
		"result<char,long> size");
_Static_assert (_Alignof (result_char_long) == _Alignof (long),
		"result<char,long> align");
