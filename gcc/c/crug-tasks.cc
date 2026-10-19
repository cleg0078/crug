/* CRUG concurrent task statements: lowering of 'go' and 'yield'.

   A 'go' statement runs its callee on the Crug task runtime
   (libcrug/core/task.h).  The callee and its arguments are captured into a
   frame record; a generated function takes the frame back out and calls the
   callee; the runtime spawns that generated function.  A task therefore
   costs one frame copy and one indirect call, and the C ABI of the callee is
   untouched (ADR 0018 D4, ADR 0005 D7).

   Nothing here needs middle-end support: the generated function is an
   ordinary static function with a void (void *) signature, which is what
   crug_task_spawn expects.

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

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "tree.h"
#include "tree-iterator.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "langhooks.h"
#include "c-family/c-common.h"
#include "c-family/crug-common.h"
#include "diagnostic-core.h"
#include "c-tree.h"
#include "cgraph.h"
#include "function.h"
#include "crug-ownership.h"
#include "crug-tasks.h"

#include <map>
#include <set>
#include <vector>

/* The runtime entry points.  They are declared here, with the signature the
   runtime defines in libcrug/core/task.h, so that a Crug translation unit
   needs no header for them: the call is an ordinary external reference that
   libcrug resolves, exactly as if the program had declared it itself.  */

static tree
crug_runtime_decl (const char *name, tree fntype)
{
  tree decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			  get_identifier (name), fntype);

  TREE_PUBLIC (decl) = 1;
  DECL_EXTERNAL (decl) = 1;
  DECL_ARTIFICIAL (decl) = 1;
  TREE_USED (decl) = 1;
  DECL_CONTEXT (decl) = NULL_TREE;
  return decl;
}

/* void crug_task_spawn (void (*entry) (void *), const void *frame, size_t n) */

static tree
crug_task_spawn_decl (void)
{
  static tree decl;

  if (decl == NULL_TREE)
    {
      tree entry = build_pointer_type (build_function_type_list (void_type_node,
								ptr_type_node,
								NULL_TREE));
      decl = crug_runtime_decl ("crug_task_spawn",
				build_function_type_list (void_type_node, entry,
							  ptr_type_node,
							  size_type_node,
							  NULL_TREE));
    }
  return decl;
}

/* void crug_task_yield (void) */

tree
crug_yield_call (location_t loc)
{
  static tree decl;

  if (decl == NULL_TREE)
    decl = crug_runtime_decl ("crug_task_yield",
			      build_function_type_list (void_type_node,
							NULL_TREE));
  return build_call_expr_loc (loc, decl, 0);
}

/* void crug_task_spawn_scoped (void *group, void (*entry) (void *),
                                 const void *frame, size_t n) */

static tree
crug_task_spawn_scoped_decl (void)
{
  static tree decl;

  if (decl == NULL_TREE)
    {
      tree entry = build_pointer_type (build_function_type_list (void_type_node,
								ptr_type_node,
								NULL_TREE));
      decl = crug_runtime_decl ("crug_task_spawn_scoped",
				build_function_type_list (void_type_node, ptr_type_node,
							  entry, ptr_type_node,
							  size_type_node,
							  NULL_TREE));
    }
  return decl;
}

/* void *crug_scope_enter (void) */

tree
crug_scope_enter_call (location_t loc)
{
  static tree decl;

  if (decl == NULL_TREE)
    decl = crug_runtime_decl ("crug_scope_enter",
			      build_function_type_list (ptr_type_node, NULL_TREE));
  return build_call_expr_loc (loc, decl, 0);
}

/* void crug_scope_leave (void *group) */

tree
crug_scope_leave_call (location_t loc, tree handle)
{
  static tree decl;

  if (decl == NULL_TREE)
    decl = crug_runtime_decl ("crug_scope_leave",
			      build_function_type_list (void_type_node,
							ptr_type_node,
							NULL_TREE));
  handle = convert (ptr_type_node, handle);
  return build_call_expr_loc_array (loc, decl, 1, &handle);
}

/* The hidden frame variables of the 'go' statements being parsed, with the
   function they belong to.  */

static std::map<tree, tree> &
crug_go_frames (void)
{
  static std::map<tree, tree> frames;
  return frames;
}

bool
crug_go_frame_ref_p (tree t)
{
  STRIP_NOPS (t);
  if (t == NULL_TREE)
    return false;
  if (TREE_CODE (t) == COMPONENT_REF)
    t = TREE_OPERAND (t, 0);
  STRIP_NOPS (t);
  return t != NULL_TREE && TREE_CODE (t) == VAR_DECL
	 && crug_go_frames ().find (t) != crug_go_frames ().end ();
}

void
crug_go_frames_forget (tree fndecl)
{
  std::map<tree, tree> &frames = crug_go_frames ();
  for (std::map<tree, tree>::iterator it = frames.begin ();
       it != frames.end (); )
    if (it->second == fndecl)
      frames.erase (it++);
    else
      ++it;
}

/* The scope handles of the statements being parsed, innermost last, and the
   functions that contain at least one 'scope' statement.  */

static std::vector<tree> &
crug_scope_stack (void)
{
  static std::vector<tree> stack;
  return stack;
}

void
crug_scope_push (tree handle)
{
  crug_scope_stack ().push_back (handle);
}

void
crug_scope_pop (void)
{
  if (!crug_scope_stack ().empty ())
    crug_scope_stack ().pop_back ();
}

tree
crug_scope_current (void)
{
  std::vector<tree> &stack = crug_scope_stack ();
  if (stack.empty ())
    return NULL_TREE;
  /* A handle of an enclosing function cannot be joined by a task defined
     inside a nested function: that function may run at any time.  */
  tree handle = stack.back ();
  if (DECL_CONTEXT (handle) != current_function_decl)
    return NULL_TREE;
  return handle;
}

static std::set<tree> &
crug_scope_functions (void)
{
  static std::set<tree> fns;
  return fns;
}

void
crug_note_scope_function (tree fndecl)
{
  if (fndecl != NULL_TREE)
    crug_scope_functions ().insert (fndecl);
}

bool
crug_function_has_scope (tree fndecl)
{
  return fndecl != NULL_TREE
	 && crug_scope_functions ().find (fndecl)
	    != crug_scope_functions ().end ();
}

/* Build the generated function that runs one task.  It takes the frame back
   out of its 'void *' argument and calls CALLEE with the captured values.
   When FNDECL is NULL_TREE the callee is an indirect call: the first frame
   field holds the function pointer.

   The function is built as a top-level static definition so that no static
   chain or trampoline is involved: it can be handed to the runtime as a
   plain function pointer.  */

static tree
crug_build_thunk (location_t loc, tree frame_type, tree fndecl,
		  const std::vector<tree> &field_types,
		  const std::vector<tree> &fields)
{
  static unsigned int counter;
  char name[48];

  snprintf (name, sizeof name, "crug_task_thunk_%u", ++counter);

  tree thunk = build_decl (loc, FUNCTION_DECL, get_identifier (name),
			   build_function_type_list (void_type_node,
						     ptr_type_node, NULL_TREE));
  TREE_STATIC (thunk) = 1;
  TREE_USED (thunk) = 1;
  TREE_PUBLIC (thunk) = 0;
  DECL_EXTERNAL (thunk) = 0;
  DECL_ARTIFICIAL (thunk) = 1;
  DECL_CONTEXT (thunk) = NULL_TREE;
  DECL_SOURCE_LOCATION (thunk) = loc;
  DECL_INITIAL (thunk) = make_node (BLOCK);
  BLOCK_SUPERCONTEXT (DECL_INITIAL (thunk)) = thunk;

  tree param = build_decl (loc, PARM_DECL, get_identifier ("__frame"),
			   ptr_type_node);
  DECL_ARG_TYPE (param) = ptr_type_node;
  DECL_CONTEXT (param) = thunk;
  TREE_USED (param) = 1;
  DECL_ARGUMENTS (thunk) = param;

  tree result = build_decl (loc, RESULT_DECL, NULL_TREE, void_type_node);
  DECL_CONTEXT (result) = thunk;
  DECL_ARTIFICIAL (result) = 1;
  DECL_IGNORED_P (result) = 1;
  DECL_RESULT (thunk) = result;

  /* cgraph_node::finalize_function reads DECL_STRUCT_FUNCTION, and the C
     genericizer needs a function to push, so the structure has to exist
     before the body is built.  */
  push_struct_function (thunk);
  cfun->function_end_locus = loc;
  pop_cfun ();

  tree frame = build1 (INDIRECT_REF, frame_type,
		       build1 (NOP_EXPR, build_pointer_type (frame_type),
			       param));
  SET_EXPR_LOCATION (frame, loc);

  std::vector<tree> call_args;
  for (size_t i = 0; i < fields.size (); i++)
    {
      tree elem = build3 (COMPONENT_REF, field_types[i], frame, fields[i],
			  NULL_TREE);
      TREE_SIDE_EFFECTS (elem) = 1;
      SET_EXPR_LOCATION (elem, loc);
      call_args.push_back (elem);
    }

  /* CALL_EXPR_FN is a pointer to the function type: the address of a known
     function, or the captured pointer of an indirect call.  */
  tree callee_ptr;
  tree call_type;
  if (fndecl != NULL_TREE)
    {
      call_type = TREE_TYPE (fndecl);
      callee_ptr = build1 (ADDR_EXPR, build_pointer_type (call_type), fndecl);
    }
  else
    {
      /* The first capture is the function pointer of an indirect call.  */
      gcc_assert (!call_args.empty ());
      callee_ptr = call_args[0];
      call_type = TREE_TYPE (TREE_TYPE (callee_ptr));
      call_args.erase (call_args.begin ());
    }

  tree call = build_call_array_loc (loc, TREE_TYPE (call_type), callee_ptr,
				    call_args.size (),
				    call_args.empty () ? NULL : &call_args[0]);
  TREE_SIDE_EFFECTS (call) = 1;
  SET_EXPR_LOCATION (call, loc);

  tree body = alloc_stmt_list ();
  append_to_statement_list (call, &body);

  DECL_SAVED_TREE (thunk) = build3 (BIND_EXPR, void_type_node, NULL_TREE, body,
				    NULL_TREE);
  TREE_SIDE_EFFECTS (DECL_SAVED_TREE (thunk)) = 1;

  /* The same two steps finish_function takes for a parsed definition: turn
     the C tree into GENERIC, then hand the definition to cgraph, which
     gimplifies and expands it with every other function.  */
  c_genericize (thunk);
  cgraph_node::finalize_function (thunk, false);
  return thunk;
}

/* Lower 'go CALLEE (ARGS);'.  See crug-tasks.h for the contract.  */

tree
crug_go_lower (location_t loc, tree callee, tree args, tree scope)
{
  tree fndecl = NULL_TREE;
  tree fntype;

  if (callee == NULL_TREE || callee == error_mark_node)
    return NULL_TREE;

  if (TREE_CODE (callee) == FUNCTION_DECL)
    fndecl = callee;
  else if (TREE_CODE (callee) == ADDR_EXPR
	   && TREE_CODE (TREE_OPERAND (callee, 0)) == FUNCTION_DECL)
    fndecl = TREE_OPERAND (callee, 0);

  if (fndecl != NULL_TREE)
    fntype = TREE_TYPE (fndecl);
  else if (TREE_CODE (TREE_TYPE (callee)) == POINTER_TYPE
	   && TREE_CODE (TREE_TYPE (TREE_TYPE (callee))) == FUNCTION_TYPE)
    fntype = TREE_TYPE (TREE_TYPE (callee));
  else
    fntype = NULL_TREE;

  if (fntype == NULL_TREE || TREE_CODE (fntype) != FUNCTION_TYPE)
    {
      error_at (loc, "%<go%> requires a function [crug-CONC-010]");
      return NULL_TREE;
    }

  /* A task's result would be discarded, so the callee must not have one
     (ADR 0018 D4).  */
  if (TREE_CODE (TREE_TYPE (fntype)) != VOID_TYPE)
    {
      error_at (loc, "%<go%> callee must return void [crug-CONC-008]");
      return NULL_TREE;
    }

  /* The frame is built from the declared parameter types, so the callee needs
     a prototype and must not be variadic: default argument promotions cannot
     be replayed for the generated function.  */
  if (!prototype_p (fntype) || stdarg_p (fntype))
    {
      error_at (loc, "%<go%> requires a prototype for its callee [crug-CONC-013]");
      return NULL_TREE;
    }

  std::vector<tree> ptypes;
  for (tree t = TYPE_ARG_TYPES (fntype);
       t != NULL_TREE && t != void_list_node; t = TREE_CHAIN (t))
    ptypes.push_back (TREE_VALUE (t));

  std::vector<tree> values;
  for (tree t = args; t != NULL_TREE; t = TREE_CHAIN (t))
    values.push_back (TREE_VALUE (t));

  if (values.size () != ptypes.size ())
    {
      error_at (loc, "too %s arguments to concurrent task",
		values.size () < ptypes.size () ? "few" : "many");
      return NULL_TREE;
    }

  for (size_t i = 0; i < values.size (); i++)
    if (values[i] == NULL_TREE || values[i] == error_mark_node)
      return NULL_TREE;

  /* Ownership rules for a capture (MASTER_SPEC section 27.2): an owned value
     has to be moved into the task, a borrow cannot escape, and a destructor
     cannot cross the boundary.  */
  {
    std::vector<tree> parms;
    for (tree p = fndecl != NULL_TREE ? DECL_ARGUMENTS (fndecl) : NULL_TREE;
	 p != NULL_TREE; p = DECL_CHAIN (p))
      parms.push_back (p);

    for (size_t i = 0; i < values.size (); i++)
      {
	tree pdecl = i < parms.size () ? parms[i] : NULL_TREE;
	if (!crug_check_go_capture (loc, values[i], pdecl, scope))
	  return NULL_TREE;
      }
  }

  /* The frame: one unnamed field per captured value, typed like the callee
     parameter so the generated function needs no conversion.  An indirect
     call captures the function pointer first.  */
  std::vector<tree> field_types;
  std::vector<tree> field_values;
  if (fndecl == NULL_TREE)
    {
      field_types.push_back (TREE_TYPE (callee));
      field_values.push_back (callee);
    }
  for (size_t i = 0; i < values.size (); i++)
    {
      field_types.push_back (ptypes[i]);
      field_values.push_back (values[i]);
    }

  tree frame_type = make_node (RECORD_TYPE);
  TYPE_NAME (frame_type) = NULL_TREE;
  std::vector<tree> fields;
  tree last = NULL_TREE;
  for (size_t i = 0; i < field_types.size (); i++)
    {
      tree f = build_decl (loc, FIELD_DECL, NULL_TREE, field_types[i]);
      DECL_CONTEXT (f) = frame_type;
      if (last == NULL_TREE)
	TYPE_FIELDS (frame_type) = f;
      else
	DECL_CHAIN (last) = f;
      last = f;
      fields.push_back (f);
    }
  layout_type (frame_type);

  tree thunk = crug_build_thunk (loc, frame_type, fndecl, field_types, fields);

  /* The caller side.  The runtime copies the frame, so the variable only has
     to live until the call returns, and the assignment of an owned value
     moves it into the task (the ownership pass sees the move here).  */
  tree frame_var = build_decl (loc, VAR_DECL, NULL_TREE, frame_type);
  TREE_USED (frame_var) = 1;
  DECL_ARTIFICIAL (frame_var) = 1;
  DECL_IGNORED_P (frame_var) = 1;
  DECL_CONTEXT (frame_var) = current_function_decl;
  DECL_SOURCE_LOCATION (frame_var) = loc;
  crug_go_frames ()[frame_var] = current_function_decl;

  tree stmts = alloc_stmt_list ();
  for (size_t i = 0; i < field_values.size (); i++)
    {
      tree lhs = build3 (COMPONENT_REF, field_types[i], frame_var, fields[i],
			 NULL_TREE);
      SET_EXPR_LOCATION (lhs, loc);
      tree assign = build_modify_expr (loc, lhs, NULL_TREE, NOP_EXPR, loc,
				       field_values[i], NULL_TREE);
      if (assign == error_mark_node)
	return NULL_TREE;
      append_to_statement_list (assign, &stmts);
    }

  tree entry = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (thunk)), thunk);
  tree addr = convert (ptr_type_node,
		       build1 (ADDR_EXPR, build_pointer_type (frame_type),
			       frame_var));
  tree size = fold_convert (size_type_node, TYPE_SIZE_UNIT (frame_type));
  tree spawn;
  if (scope != NULL_TREE)
    {
      /* A task started inside a 'scope' joins that group; the group's wait
	 is what keeps a borrowed capture sound (ADR 0024 D4).  */
      tree handle = convert (ptr_type_node, scope);
      spawn = build_call_expr_loc_array (loc, crug_task_spawn_scoped_decl (), 4,
					 (tree[]){ handle, entry, addr, size });
    }
  else
    spawn = build_call_expr_loc_array (loc, crug_task_spawn_decl (), 3,
					(tree[]){ entry, addr, size });
  TREE_SIDE_EFFECTS (spawn) = 1;
  SET_EXPR_LOCATION (spawn, loc);
  append_to_statement_list (spawn, &stmts);

  return c_build_bind_expr (loc, frame_var, stmts);
}
