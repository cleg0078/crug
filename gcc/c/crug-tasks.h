/* CRUG concurrent task statements: the 'go' statement and the 'yield'
   intrinsic.  See crug-tasks.cc for the lowering.

   The lowering is a front-end only transformation: a task is a generated
   function plus a frame record, and the runtime call that starts it.  No
   middle-end or back-end support is needed, so a translation unit that does
   not use 'go' is unchanged (ADR 0018 D4).

   Copyright (C) 2026 CLEG0078

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

#ifndef GCC_CRUG_TASKS_H
#define GCC_CRUG_TASKS_H

/* Lower one 'go' statement.  CALLEE is the callee expression: a FUNCTION_DECL
   for 'go f (a);', or an expression of function pointer type for
   'go ((*fp) (a));'.  ARGS is a TREE_LIST of the argument expressions in
   source order, or NULL_TREE when the task takes no argument.  Returns the
   statement to add, or NULL_TREE after reporting a diagnostic.  */
extern tree crug_go_lower (location_t loc, tree callee, tree args);

/* The runtime call that a 'yield ();' statement lowers to.  */
extern tree crug_yield_call (location_t loc);

#endif /* GCC_CRUG_TASKS_H */
