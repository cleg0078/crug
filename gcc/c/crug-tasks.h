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
   source order, or NULL_TREE when the task takes no argument.  SCOPE is the
   group handle of the enclosing 'scope' statement, or NULL_TREE outside one:
   a scoped task joins that group, which is what makes a borrowed capture
   sound (ADR 0024 D4).  Returns the statement to add, or NULL_TREE after
   reporting a diagnostic.  */
extern tree crug_go_lower (location_t loc, tree callee, tree args, tree scope);

/* The runtime calls of a 'scope' statement: enter creates the group and
   leave waits for every task in it (ADR 0024 D3).  */
extern tree crug_scope_enter_call (location_t loc);
extern tree crug_scope_leave_call (location_t loc, tree handle);

/* The 'scope' handles of the statements being parsed, innermost last.  The
   parser pushes a handle before the body and pops it after, so a 'go'
   statement lowers into the nearest enclosing scope (or none).  */
extern void crug_scope_push (tree handle);
extern void crug_scope_pop (void);
extern tree crug_scope_current (void);

/* True when T is (a member of) the hidden frame variable of a 'go'
   statement.  The frame is a transport object, not an ordinary memory
   location: an owned value is moved into it, and a scoped borrow is
   captured as the pointer the task receives (ADR 0024 D4).  */
extern bool crug_go_frame_ref_p (tree t);

/* Forget the frames of FNDECL once its analysis is done.  */
extern void crug_go_frames_forget (tree fndecl);

/* Remember that FNDECL contains a 'scope' statement, and query it: such a
   function needs the ownership pass even when only -fcrug-goroutines is on,
   because the analysis is what places the waits.  */
extern void crug_note_scope_function (tree fndecl);
extern bool crug_function_has_scope (tree fndecl);

/* The runtime call that a 'yield ();' statement lowers to.  */
extern tree crug_yield_call (location_t loc);

#endif /* GCC_CRUG_TASKS_H */
