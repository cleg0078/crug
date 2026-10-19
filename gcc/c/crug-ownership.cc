/* Crug ownership analysis (Phase 4 MVP).
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

/* The Phase 4 ownership MVP: 'own' declarators and the move() intrinsic.
   The analysis is a single forward pass over the GENERIC body of a function
   before the middle end sees it.  It tracks, for every local owning object,
   whether the object currently owns its value (OWNED), has been moved out of
   (MOVED) or might be either after a branch join (MAYBE_MOVED).  No state is
   stored in the object: ADR 0003 and ADR 0004 forbid hidden fields, hidden
   bits, reference counts and drop flags, so the whole model lives in the
   compiler and disappears when checking ends.  move() is lowered to its
   operand at the end of the pass, so it generates no code.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "options.h"
#include "tree.h"
#include "tree-iterator.h"
#include "input.h"
#include "stringpool.h"
#include "c-family/c-common.h"
#include "c-family/crug-common.h"
#include "diagnostic-core.h"
#include "toplev.h"
#include "crug-ownership.h"

#include <map>
#include <set>
#include <vector>

/* The ownership state of one tracked object.  The names are also the names
   printed by -fcrug-dump=ownership, so they are part of the observable
   interface of this file (the Phase 4 gate greps for them).  */

enum crug_state
{
  CRUG_UNINIT,
  CRUG_OWNED,
  CRUG_MOVED,
  CRUG_MAYBE_MOVED
};

typedef std::map<tree, crug_state> state_map;

struct crug_fn_ctx
{
  /* The function being checked.  */
  tree fndecl;
  /* Every owning object that has been declared so far, in declaration
     order, so that dumps and joins are deterministic.  */
  std::vector<tree> owned;
  std::set<tree> seen;
  /* Environments recorded for 'break' statements, consumed by the loop or
     switch that owns them.  */
  std::vector<state_map> breaks;
  /* One entry per enclosing switch; each case begins from a merge of the
     switch entry state and the state falling into it.  */
  std::vector<state_map> case_envs;
};

/* Declarations recorded as owning during parsing, keyed by DECL_UID rather
   than by tree pointer: the garbage collector may recycle a tree node, and a
   recycled node must never be mistaken for a declaration that carried the
   qualifier.  DECL_UID values are never reused.  */

static std::set<unsigned int> crug_owning_uids;

/* The artificial function declaration used for an unlowered move().  */

static tree crug_move_fndecl;

bool
crug_ownership_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_OWNERSHIP);
}

/* Building and recognising the move intrinsic.  */

tree
crug_build_move_call (location_t loc, tree arg)
{
  if (crug_move_fndecl == NULL_TREE)
    {
      tree fntype = build_function_type_list (ptr_type_node, ptr_type_node,
					      NULL_TREE);
      crug_move_fndecl = build_decl (loc, FUNCTION_DECL,
				     get_identifier ("__builtin_crug_move"),
				     fntype);
      DECL_ARTIFICIAL (crug_move_fndecl) = 1;
      DECL_EXTERNAL (crug_move_fndecl) = 1;
      TREE_PUBLIC (crug_move_fndecl) = 0;
    }

  tree call = build_call_expr_loc (loc, crug_move_fndecl, 1, arg);
  /* The intrinsic is transparent: its result has the type of its operand.  */
  TREE_TYPE (call) = TREE_TYPE (arg);
  return call;
}

static bool
is_move_call (tree t)
{
  if (t == NULL_TREE || TREE_CODE (t) != CALL_EXPR)
    return false;

  tree fn = CALL_EXPR_FN (t);
  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
    fn = TREE_OPERAND (fn, 0);
  return fn != NULL_TREE && fn == crug_move_fndecl;
}

static tree
lower_move_cb (tree *tp, int *walk_subtrees, void *data)
{
  (void) data;

  if (is_move_call (*tp))
    {
      *tp = CALL_EXPR_ARG (*tp, 0);
      *walk_subtrees = 1;
    }
  return NULL_TREE;
}

/* Owning declarations.  */

void
crug_mark_owning_decl (tree decl, location_t loc)
{
  if (decl == NULL_TREE || decl == error_mark_node || !DECL_P (decl))
    return;

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      error_at (loc, "qualifier is not allowed on this declarator "
		"[crug-TYPE-001]");
      return;
    }

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      /* An owning return value.  The value is not tracked in the MVP.  */
      crug_owning_uids.insert (DECL_UID (decl));
      return;
    }

  tree type = TREE_TYPE (decl);
  if (type == NULL_TREE || type == error_mark_node)
    return;

  tree t = type;
  while (TREE_CODE (t) == ARRAY_TYPE)
    t = TREE_TYPE (t);

  if (TREE_CODE (t) != POINTER_TYPE)
    {
      error_at (loc, "qualifier is not allowed on this declarator "
		"[crug-TYPE-001]");
      return;
    }

  if (TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE)
    {
      error_at (loc, "'own' may not qualify a function type [crug-TYPE-005]");
      return;
    }

  crug_owning_uids.insert (DECL_UID (decl));
}

bool
crug_is_owning_decl (tree decl)
{
  return (decl != NULL_TREE && DECL_P (decl)
	  && crug_owning_uids.count (DECL_UID (decl)) != 0);
}

/* States.  */

static crug_state
state_of (const state_map &env, tree decl)
{
  state_map::const_iterator it = env.find (decl);
  return it == env.end () ? CRUG_UNINIT : it->second;
}

static void
set_state (state_map &env, tree decl, crug_state state)
{
  env[decl] = state;
}

static const char *
state_name (crug_state state)
{
  switch (state)
    {
    case CRUG_UNINIT:
      return "UNINIT";
    case CRUG_OWNED:
      return "OWNED";
    case CRUG_MOVED:
      return "MOVED";
    case CRUG_MAYBE_MOVED:
      return "MAYBE_MOVED";
    }
  return "?";
}

static crug_state
merge_state (crug_state a, crug_state b)
{
  if (a == b)
    return a;
  if ((a == CRUG_UNINIT && b == CRUG_OWNED)
      || (a == CRUG_OWNED && b == CRUG_UNINIT))
    return CRUG_UNINIT;
  return CRUG_MAYBE_MOVED;
}

/* Dump.  */

static bool
dump_enabled (void)
{
  return crug_dump_kind != NULL && strcmp (crug_dump_kind, "ownership") == 0;
}

static FILE *
get_dump_file (void)
{
  static FILE *file = NULL;
  static bool tried = false;

  if (tried)
    return file;
  tried = true;

  if (main_input_filename == NULL)
    return NULL;

  const char *base = lbasename (main_input_filename);
  const char *dot = strrchr (base, '.');
  size_t stem = dot != NULL ? (size_t) (dot - base) : strlen (base);
  char *name = new char[stem + sizeof (".crug-ownership")];
  memcpy (name, base, stem);
  memcpy (name + stem, ".crug-ownership", sizeof (".crug-ownership"));
  file = fopen (name, "w");
  if (file == NULL)
    error ("cannot open the Crug ownership dump %qs: %m", name);
  else
    fprintf (file, "crug ownership dump v1\n");
  delete[] name;
  return file;
}

static void
dump_join (location_t loc, tree decl, crug_state a, crug_state b,
	   crug_state result)
{
  if (!dump_enabled ())
    return;

  FILE *file = get_dump_file ();
  if (file == NULL || DECL_NAME (decl) == NULL_TREE)
    return;

  /* Print the owning state first so that the dump line is canonical.  */
  if (a != CRUG_OWNED && b == CRUG_OWNED)
    {
      crug_state tmp = a;
      a = b;
      b = tmp;
    }

  expanded_location where = expand_location (loc);
  fprintf (file, "join %s:%d: %s=%s/%s -> %s\n",
	   where.file != NULL ? lbasename (where.file) : "<unknown>",
	   where.line,
	   IDENTIFIER_POINTER (DECL_NAME (decl)),
	   state_name (a), state_name (b), state_name (result));
}

/* The analysis proper.  */

static bool walk_stmt (crug_fn_ctx &ctx, state_map &env, tree t);
static void walk_expr (crug_fn_ctx &ctx, state_map &env, tree t,
		       location_t locus = UNKNOWN_LOCATION);

static void
register_owned (crug_fn_ctx &ctx, state_map &env, tree decl)
{
  if (ctx.seen.insert (decl).second)
    {
      ctx.owned.push_back (decl);
      if (env.find (decl) == env.end ())
	env[decl] = CRUG_UNINIT;
    }
}

static void
merge_into (crug_fn_ctx &ctx, state_map &dst, const state_map &other,
	    location_t loc)
{
  for (tree decl : ctx.owned)
    {
      crug_state a = state_of (dst, decl);
      crug_state b = state_of (other, decl);
      crug_state result = merge_state (a, b);
      if (a != b)
	dump_join (loc, decl, a, b, result);
      dst[decl] = result;
    }
}

static void
check_use (state_map &env, tree decl, location_t loc)
{
  crug_state state = state_of (env, decl);

  if (state == CRUG_MOVED)
    error_at (loc, "use of moved value %qD [crug-UAM-001]", decl);
  else if (state == CRUG_MAYBE_MOVED)
    error_at (loc, "use of possibly-moved value %qD after a branch join "
	      "[crug-UAM-004]", decl);
}

static location_t
node_location (tree t)
{
  location_t loc = EXPR_LOCATION (t);
  return loc == UNKNOWN_LOCATION ? input_location : loc;
}

static void
process_move (crug_fn_ctx &ctx, state_map &env, tree arg, location_t loc)
{
  tree a = arg;
  STRIP_NOPS (a);

  if (TREE_CODE (a) == VAR_DECL || TREE_CODE (a) == PARM_DECL)
    {
      if (!crug_is_owning_decl (a))
	{
	  error_at (loc, "%<move%> operand is not an owned object "
		    "[crug-OWN-001]");
	  return;
	}

      crug_state state = state_of (env, a);
      if (state == CRUG_MOVED)
	{
	  error_at (loc, "move of already-moved value %qD [crug-UAM-002]", a);
	  return;
	}
      if (state == CRUG_MAYBE_MOVED)
	{
	  error_at (loc, "use of possibly-moved value %qD after a branch join "
		    "[crug-UAM-004]", a);
	  return;
	}

      set_state (env, a, CRUG_MOVED);
      return;
    }

  if (TREE_CODE (a) == COMPONENT_REF
      || TREE_CODE (a) == ARRAY_REF
      || TREE_CODE (a) == INDIRECT_REF
      || TREE_CODE (a) == MEM_REF)
    {
      error_at (loc, "move of aggregate field %qE is not supported; "
		"move the whole object [crug-UAM-006]", a);
      return;
    }

  /* A prvalue that owns its value (for example a call returning own) is
     consumed by the move; there is no source object to mark.  */
  walk_expr (ctx, env, a, loc);
}

/* Handle an expression used as a value.  */

static void
walk_expr (crug_fn_ctx &ctx, state_map &env, tree t, location_t locus)
{
  if (t == NULL_TREE || t == error_mark_node)
    return;

  /* Operands such as a VAR_DECL carry no location of their own, so the
     innermost enclosing expression supplies the location of the use.  */
  if (EXPR_HAS_LOCATION (t))
    locus = EXPR_LOCATION (t);

  switch (TREE_CODE (t))
    {
    case CALL_EXPR:
      if (is_move_call (t))
	{
	  process_move (ctx, env, CALL_EXPR_ARG (t, 0), node_location (t));
	  return;
	}
      for (int i = 0; i < call_expr_nargs (t); i++)
	walk_expr (ctx, env, CALL_EXPR_ARG (t, i), locus);
      if (CALL_EXPR_FN (t) != NULL_TREE
	  && TREE_CODE (CALL_EXPR_FN (t)) != ADDR_EXPR)
	walk_expr (ctx, env, CALL_EXPR_FN (t), locus);
      return;

    case MODIFY_EXPR:
    case INIT_EXPR:
      {
	tree lhs = TREE_OPERAND (t, 0);
	tree rhs = TREE_OPERAND (t, 1);
	tree l = lhs;
	STRIP_NOPS (l);
	location_t loc = node_location (t);

	if ((TREE_CODE (l) == VAR_DECL || TREE_CODE (l) == PARM_DECL)
	    && crug_is_owning_decl (l))
	  {
	    if (rhs == NULL_TREE)
	      {
		set_state (env, l, CRUG_UNINIT);
		return;
	      }

	    if (is_move_call (rhs))
	      {
		process_move (ctx, env, CALL_EXPR_ARG (rhs, 0), loc);
		set_state (env, l, CRUG_OWNED);
		return;
	      }

	    tree r = rhs;
	    STRIP_NOPS (r);
	    if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
		&& crug_is_owning_decl (r))
	      {
		crug_state state = state_of (env, r);
		if (state == CRUG_MOVED)
		  error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
		else if (state == CRUG_MAYBE_MOVED)
		  error_at (loc, "use of possibly-moved value %qD after a "
			    "branch join [crug-UAM-004]", r);
		else
		  error_at (loc, "implicit copy of owned value %qD; owned "
			    "values are not copyable [crug-UAM-003]", r);
		set_state (env, l, CRUG_OWNED);
		return;
	      }

	    walk_expr (ctx, env, rhs);
	    set_state (env, l, CRUG_OWNED);
	    return;
	  }

	walk_expr (ctx, env, lhs, locus);
	if (rhs != NULL_TREE)
	  walk_expr (ctx, env, rhs, loc);
	return;
      }

    case COND_EXPR:
      {
	if (VOID_TYPE_P (TREE_TYPE (t)))
	  {
	    walk_expr (ctx, env, TREE_OPERAND (t, 0), locus);
	    state_map a = env;
	    state_map b = env;
	    bool ra = walk_stmt (ctx, a, TREE_OPERAND (t, 1));
	    bool rb = walk_stmt (ctx, b, TREE_OPERAND (t, 2));
	    if (ra && rb)
	      {
		state_map merged = a;
		merge_into (ctx, merged, b, node_location (t));
		env = merged;
	      }
	    else if (ra)
	      env = a;
	    else if (rb)
	      env = b;
	    return;
	  }

	walk_expr (ctx, env, TREE_OPERAND (t, 0), locus);
	state_map a = env;
	state_map b = env;
	walk_expr (ctx, a, TREE_OPERAND (t, 1), locus);
	walk_expr (ctx, b, TREE_OPERAND (t, 2), locus);
	merge_into (ctx, env, a, node_location (t));
	merge_into (ctx, env, b, node_location (t));
	return;
      }

    case COMPOUND_EXPR:
      walk_expr (ctx, env, TREE_OPERAND (t, 0), locus);
      walk_expr (ctx, env, TREE_OPERAND (t, 1), locus);
      return;

    case VAR_DECL:
    case PARM_DECL:
      if (crug_is_owning_decl (t))
	check_use (env, t,
		   locus != UNKNOWN_LOCATION ? locus : node_location (t));
      return;

    default:
      {
	if (DECL_P (t) || TYPE_P (t) || TREE_CODE_CLASS (TREE_CODE (t)) == tcc_constant)
	  return;
	int n = TREE_CODE_LENGTH (TREE_CODE (t));
	for (int i = 0; i < n; i++)
	  if (TREE_OPERAND (t, i) != NULL_TREE)
	    walk_expr (ctx, env, TREE_OPERAND (t, i), locus);
	return;
      }
    }
}

/* Handle a statement, or an expression used as a statement.  Return true when
   control can continue after it.  */

static bool
walk_stmt (crug_fn_ctx &ctx, state_map &env, tree t)
{
  if (t == NULL_TREE || t == error_mark_node)
    return true;

  location_t loc = node_location (t);

  switch (TREE_CODE (t))
    {
    case BIND_EXPR:
      for (tree v = BIND_EXPR_VARS (t); v != NULL_TREE; v = DECL_CHAIN (v))
	if (TREE_CODE (v) == VAR_DECL && crug_is_owning_decl (v))
	  register_owned (ctx, env, v);
      return walk_stmt (ctx, env, BIND_EXPR_BODY (t));

    case STATEMENT_LIST:
      {
	bool reachable = true;
	for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	  {
	    tree stmt = tsi_stmt (i);
	    if (!reachable
		&& (TREE_CODE (stmt) == LABEL_EXPR
		    || TREE_CODE (stmt) == CASE_LABEL_EXPR))
	      reachable = true;
	    if (!reachable)
	      continue;
	    reachable = walk_stmt (ctx, env, stmt);
	  }
	return reachable;
      }

    case DECL_EXPR:
      {
	tree decl = DECL_EXPR_DECL (t);
	if (decl == NULL_TREE
	    || TREE_CODE (decl) != VAR_DECL
	    || !crug_is_owning_decl (decl))
	  return true;

	register_owned (ctx, env, decl);
	tree init = DECL_INITIAL (decl);
	if (init == NULL_TREE || init == error_mark_node)
	  {
	    set_state (env, decl, CRUG_UNINIT);
	    return true;
	  }

	if (TREE_CODE (init) == CONSTRUCTOR)
	  {
	    set_state (env, decl, CRUG_OWNED);
	    return true;
	  }

	if (is_move_call (init))
	  {
	    process_move (ctx, env, CALL_EXPR_ARG (init, 0), loc);
	    set_state (env, decl, CRUG_OWNED);
	    return true;
	  }

	tree r = init;
	STRIP_NOPS (r);
	if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
	    && crug_is_owning_decl (r))
	  {
	    crug_state state = state_of (env, r);
	    if (state == CRUG_MOVED)
	      error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
	    else if (state == CRUG_MAYBE_MOVED)
	      error_at (loc, "use of possibly-moved value %qD after a branch "
			"join [crug-UAM-004]", r);
	    else
	      error_at (loc, "implicit copy of owned value %qD; owned values "
			"are not copyable [crug-UAM-003]", r);
	    set_state (env, decl, CRUG_OWNED);
	    return true;
	  }

	walk_expr (ctx, env, init, loc);
	set_state (env, decl, CRUG_OWNED);
	return true;
      }

    case RETURN_EXPR:
      if (TREE_OPERAND (t, 0) != NULL_TREE)
	walk_expr (ctx, env, TREE_OPERAND (t, 0), loc);
      return false;

    case COND_EXPR:
      {
	if (!VOID_TYPE_P (TREE_TYPE (t)))
	  {
	    walk_expr (ctx, env, t, loc);
	    return true;
	  }

	walk_expr (ctx, env, TREE_OPERAND (t, 0), loc);

	state_map then_env = env;
	state_map else_env = env;
	bool then_reach = walk_stmt (ctx, then_env, TREE_OPERAND (t, 1));
	bool else_reach = walk_stmt (ctx, else_env, TREE_OPERAND (t, 2));

	if (!then_reach && !else_reach)
	  return false;
	if (then_reach && else_reach)
	  {
	    merge_into (ctx, env, then_env, loc);
	    merge_into (ctx, env, else_env, loc);
	  }
	else if (then_reach)
	  env = then_env;
	else
	  env = else_env;
	return true;
      }

    case FOR_STMT:
      {
	if (FOR_INIT_STMT (t) != NULL_TREE)
	  walk_stmt (ctx, env, FOR_INIT_STMT (t));
	if (FOR_COND (t) != NULL_TREE)
	  walk_expr (ctx, env, FOR_COND (t), loc);

	size_t break_mark = ctx.breaks.size ();
	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, FOR_BODY (t));
	if (FOR_EXPR (t) != NULL_TREE)
	  walk_expr (ctx, body, FOR_EXPR (t), loc);

	state_map merged;
	bool have = false;
	if (FOR_COND (t) != NULL_TREE)
	  {
	    merged = env;
	    have = true;
	  }
	if (body_reach)
	  {
	    if (have)
	      merge_into (ctx, merged, body, loc);
	    else
	      merged = body, have = true;
	  }
	for (size_t i = break_mark; i < ctx.breaks.size (); i++)
	  {
	    if (have)
	      merge_into (ctx, merged, ctx.breaks[i], loc);
	    else
	      merged = ctx.breaks[i], have = true;
	  }
	ctx.breaks.resize (break_mark);
	if (!have)
	  return false;
	env = merged;
	return true;
      }

    case WHILE_STMT:
      {
	walk_expr (ctx, env, WHILE_COND (t), loc);

	size_t break_mark = ctx.breaks.size ();
	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, WHILE_BODY (t));

	state_map merged = env;
	if (body_reach)
	  merge_into (ctx, merged, body, loc);
	for (size_t i = break_mark; i < ctx.breaks.size (); i++)
	  merge_into (ctx, merged, ctx.breaks[i], loc);
	ctx.breaks.resize (break_mark);
	env = merged;
	return true;
      }

    case DO_STMT:
      {
	size_t break_mark = ctx.breaks.size ();
	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, DO_BODY (t));
	walk_expr (ctx, body, DO_COND (t), loc);

	state_map merged;
	bool have = false;
	if (body_reach)
	  {
	    merged = body;
	    have = true;
	  }
	for (size_t i = break_mark; i < ctx.breaks.size (); i++)
	  {
	    if (have)
	      merge_into (ctx, merged, ctx.breaks[i], loc);
	    else
	      merged = ctx.breaks[i], have = true;
	  }
	ctx.breaks.resize (break_mark);
	if (!have)
	  return false;
	env = merged;
	return true;
      }

    case SWITCH_STMT:
      {
	walk_expr (ctx, env, SWITCH_STMT_COND (t), loc);

	size_t break_mark = ctx.breaks.size ();
	ctx.case_envs.push_back (env);
	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, SWITCH_STMT_BODY (t));
	ctx.case_envs.pop_back ();

	/* Without a default label no case need match.  */
	state_map merged = env;
	if (body_reach)
	  merge_into (ctx, merged, body, loc);
	for (size_t i = break_mark; i < ctx.breaks.size (); i++)
	  merge_into (ctx, merged, ctx.breaks[i], loc);
	ctx.breaks.resize (break_mark);
	env = merged;
	return true;
      }

    case CASE_LABEL_EXPR:
      if (!ctx.case_envs.empty ())
	{
	  state_map entry = ctx.case_envs.back ();
	  state_map fall = env;
	  merge_into (ctx, entry, fall, loc);
	  env = entry;
	}
      return true;

    case BREAK_STMT:
      ctx.breaks.push_back (env);
      return false;

    case CONTINUE_STMT:
      return false;

    case GOTO_EXPR:
      return false;

    case LABEL_EXPR:
      /* A label is a jump target.  The MVP does not model back edges, so the
	 state at the label is the state of the textually preceding path.  */
      return true;

    default:
      if (EXPR_P (t))
	{
	  walk_expr (ctx, env, t, loc);
	  return true;
	}
      return true;
    }
}

/* Entry point.  */

void
crug_check_function (tree fndecl, tree body)
{
  if (fndecl == NULL_TREE || body == NULL_TREE)
    return;

  crug_fn_ctx ctx;
  ctx.fndecl = fndecl;

  state_map env;
  walk_stmt (ctx, env, body);

  if (dump_enabled () && !ctx.owned.empty ())
    {
      FILE *file = get_dump_file ();
      if (file != NULL)
	{
	  fprintf (file, "function %s\n", IDENTIFIER_POINTER (DECL_NAME (fndecl)));
	  for (tree decl : ctx.owned)
	    fprintf (file, "exit %s: %s\n",
		     IDENTIFIER_POINTER (DECL_NAME (fndecl)),
		     DECL_NAME (decl) != NULL_TREE
		       ? IDENTIFIER_POINTER (DECL_NAME (decl)) : "<unnamed>");
	  fflush (file);
	}
    }

  /* Lower move() away.  The intrinsic must not survive the front end: the
     middle end would not know it and a symbol would be emitted.  */
  if (crug_move_fndecl != NULL_TREE)
    walk_tree_without_duplicates (&body, lower_move_cb, NULL);

}
