/* Crug ownership and drop analysis (Phase 5).
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

/* The Crug ownership and drop pass.

   Phase 4 decided, for every local owning object, whether the object
   currently owns a value (OWNED), has been moved out of (MOVED), owns
   nothing (EMPTY) or is in a state that depends on the branch taken.  Phase 5
   adds the other half of the ownership model: every edge that leaves the
   scope of an owning object destroys it, in reverse declaration order, by
   calling its destructor.

   Two facts make this work without any runtime state.  First, the destructor
   of an object is a compile-time fact recorded in the analysis environment
   (ADR 0005, amendment A-1); it is never stored in the object.  Second, the
   liveness of an object on an edge is a compile-time fact of the ownership
   state machine.  A drop is therefore emitted as a direct, null-guarded call
   'if (p) dtor (p);' with no helper, no flag and no registry, which is what
   ADR 0003 D5 forbids and what RFC-0001 section 19 requires.

   The pass runs after parsing and before gimplification:

     pass A  walks the GENERIC body, maintains the ownership state and the
	     destructor of every owning object, records every exit edge with
	     the list of objects it must destroy, and writes -fcrug-dump;
     pass B  rewrites the body in place: exit edges get their destruction
	     statements, and the MOVE, ADOPT, RELEASE and DROP calls are
	     removed so that no intrinsic reaches the middle end.

   Nothing is stored in the object and nothing is generated for the
   qualifier itself, so the C ABI of an 'own' pointer is the C ABI of a
   pointer (ADR 0006).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "options.h"
#include "tree.h"
#include "tree-iterator.h"
#include "input.h"
#include "stringpool.h"
#include "fold-const.h"
#include "c-family/c-common.h"
#include "c-family/crug-common.h"
#include "diagnostic-core.h"
#include "toplev.h"
#include "flags.h"
#include "crug-ownership.h"

#include <map>
#include <set>
#include <vector>

/* The ownership state of one tracked object.  The names are part of the
   observable interface of this file (the Phase 4 gate greps the dump for
   them), so they are not renumbered.  */

enum crug_state
{
  CRUG_EMPTY,
  CRUG_OWNED,
  CRUG_MOVED,
  CRUG_MAYBE_MOVED,
  CRUG_MAYBE_EMPTY
};

/* One tracked object: its state and the destructor that would be called if
   the object is destroyed here.  DTOR is NULL_TREE when no destructor is
   known, which is not the same as "no destruction needed": an OWNED object
   without a destructor leaks, and the -Wcrug-unadopted-own warning says so.  */

struct crug_ostate
{
  crug_state state;
  tree dtor;

  crug_ostate () : state (CRUG_EMPTY), dtor (NULL_TREE) {}
  crug_ostate (crug_state s, tree d) : state (s), dtor (d) {}
};

typedef std::map<tree, crug_ostate> state_map;

/* One destruction that an exit edge performs.  */

struct crug_drop
{
  tree decl;
  tree dtor;
};

enum crug_exit_kind
{
  CRUG_EXIT_SCOPE_END,
  CRUG_EXIT_RETURN,
  CRUG_EXIT_BREAK,
  CRUG_EXIT_CONTINUE,
  CRUG_EXIT_GOTO
};

/* One exit edge and the destructions it performs.  */

struct crug_exit
{
  tree stmt;
  enum crug_exit_kind kind;
  location_t loc;
  std::vector<crug_drop> drops;
  bool has_value;
};

struct crug_frame
{
  tree bind;
  std::vector<tree> decls;
  std::vector<tree> borrows;
};

/* A loop or switch: where break and continue land, and the states that
   arrive there.  */

struct crug_target
{
  bool is_loop;
  size_t keep_depth;
  std::vector<state_map> edges;
  state_map cases;
};

struct crug_goto
{
  tree stmt;
  tree label;
  location_t loc;
  state_map env;
};

struct crug_label_info
{
  std::vector<tree> path;
  std::vector<tree> decls_before;
};

/* A live borrow: which binding holds it and which object it borrows.  A
   borrow is live from its initialization to the end of the innermost block
   that declares it, so liveness is structural and needs no dataflow (this
   phase is lexical; RFC-0001 section 18.4 asks for last-use analysis in a
   later phase).  LENDER is the root object borrowed, never an intermediate
   reborrow, and is NULL_TREE for a borrowed parameter, which has no local
   lender.  DEPTH is the number of enclosing frames at the declaration.  */

struct crug_borrow
{
  bool is_mut;
  tree lender;
  size_t depth;
};

struct crug_fn_ctx
{
  tree fndecl;
  tree body;
  std::vector<crug_frame> frames;
  std::map<tree, crug_exit> exits;
  std::map<tree, std::vector<crug_drop> > scope_drops;
  std::map<tree, std::vector<crug_drop> > explicit_drops;
  std::map<tree, tree> stmt_replace;
  std::map<tree, state_map> label_envs;
  std::map<tree, crug_label_info> labels;
  std::vector<crug_goto> gotos;
  std::vector<crug_target> targets;
  std::set<tree> warned;
  std::set<tree> warned_setjmp;
  /* Live borrows, keyed by the borrowing binding.  An entry disappears when
     the block that declares the binding ends, which is what makes the
     checker lexical.  */
  std::map<tree, crug_borrow> borrows;
  /* Bindings declared with ref or mut, initialized or not, and the frame
     depth of every owning or borrowing declaration that has a lifetime.  */
  std::set<tree> borrow_declared;
  std::map<tree, size_t> decl_depth;
};

/* Declarations recorded as owning during parsing, keyed by DECL_UID rather
   than by tree pointer: the garbage collector may recycle a tree node, and a
   recycled node must never be mistaken for a declaration that carried the
   qualifier.  DECL_UID values are never reused.  */

static std::set<unsigned int> crug_owning_uids;

/* Declarations recorded as borrows during parsing, keyed the same way.  The
   value says whether the borrow is exclusive.  This is analysis metadata:
   nothing is attached to the type or to the object (ADR 0003 D1).  */
static std::map<unsigned int, bool> crug_borrow_uids;

/* The artificial function declarations used by the intrinsics.  They exist
   only inside the front end: every call is removed by this pass.  */

static tree crug_intr_fndecl[CRUG_INTR_BORROW_MUT + 1];

bool
crug_ownership_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_OWNERSHIP);
}

bool
crug_borrow_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_BORROW);
}

/* Intrinsic names and calls.  */

bool
crug_intrinsic_kind (tree id, enum crug_intrinsic *kind)
{
  if (id == NULL_TREE || TREE_CODE (id) != IDENTIFIER_NODE)
    return false;

  const char *name = IDENTIFIER_POINTER (id);
  enum crug_intrinsic k;

  if (strcmp (name, "move") == 0)
    k = CRUG_INTR_MOVE;
  else if (strcmp (name, "adopt") == 0)
    k = CRUG_INTR_ADOPT;
  else if (strcmp (name, "release") == 0)
    k = CRUG_INTR_RELEASE;
  else if (strcmp (name, "drop") == 0)
    k = CRUG_INTR_DROP;
  else if (strcmp (name, "borrow") == 0)
    k = CRUG_INTR_BORROW;
  else if (strcmp (name, "borrow_mut") == 0)
    k = CRUG_INTR_BORROW_MUT;
  else
    return false;

  if (kind != NULL)
    *kind = k;
  return true;
}

bool
crug_intrinsic_enabled (enum crug_intrinsic kind)
{
  switch (kind)
    {
    case CRUG_INTR_BORROW:
    case CRUG_INTR_BORROW_MUT:
      return crug_borrow_enabled ();
    default:
      return crug_ownership_enabled ();
    }
}

static const char *
intrinsic_name (enum crug_intrinsic kind)
{
  switch (kind)
    {
    case CRUG_INTR_MOVE:
      return "__builtin_crug_move";
    case CRUG_INTR_ADOPT:
      return "__builtin_crug_adopt";
    case CRUG_INTR_RELEASE:
      return "__builtin_crug_release";
    case CRUG_INTR_DROP:
      return "__builtin_crug_drop";
    case CRUG_INTR_BORROW:
      return "__builtin_crug_borrow";
    case CRUG_INTR_BORROW_MUT:
      return "__builtin_crug_borrow_mut";
    }
  return "__builtin_crug_move";
}

static tree
intrinsic_fndecl (enum crug_intrinsic kind, location_t loc)
{
  if (crug_intr_fndecl[kind] != NULL_TREE)
    return crug_intr_fndecl[kind];

  tree fntype;
  switch (kind)
    {
    case CRUG_INTR_DROP:
      fntype = build_function_type_list (void_type_node, ptr_type_node,
					 NULL_TREE);
      break;
    case CRUG_INTR_ADOPT:
      fntype = build_function_type_list (ptr_type_node, ptr_type_node,
					 ptr_type_node, NULL_TREE);
      break;
    default:
      fntype = build_function_type_list (ptr_type_node, ptr_type_node,
					 NULL_TREE);
      break;
    }

  tree fndecl = build_decl (loc, FUNCTION_DECL,
			    get_identifier (intrinsic_name (kind)), fntype);
  DECL_ARTIFICIAL (fndecl) = 1;
  DECL_EXTERNAL (fndecl) = 1;
  TREE_PUBLIC (fndecl) = 0;
  crug_intr_fndecl[kind] = fndecl;
  return fndecl;
}

tree
crug_build_intrinsic_call (enum crug_intrinsic kind, location_t loc,
			   tree arg0, tree arg1)
{
  tree fndecl = intrinsic_fndecl (kind, loc);
  tree call;

  if (kind == CRUG_INTR_ADOPT)
    {
      /* A function designator has not been decayed here, because the
	 intrinsic has no declared parameter type to convert to.  */
      if (arg1 != NULL_TREE && arg1 != error_mark_node
	  && TREE_CODE (arg1) == FUNCTION_DECL)
	arg1 = build1 (ADDR_EXPR, build_pointer_type (TREE_TYPE (arg1)), arg1);
      call = build_call_expr_loc (loc, fndecl, 2, arg0, arg1);
    }
  else
    call = build_call_expr_loc (loc, fndecl, 1, arg0);

  /* move, adopt and release are transparent: the result has the type of the
     first operand.  drop returns void.  */
  if (kind == CRUG_INTR_DROP)
    TREE_TYPE (call) = void_type_node;
  else if (arg0 != NULL_TREE && arg0 != error_mark_node)
    TREE_TYPE (call) = TREE_TYPE (arg0);
  return call;
}

static bool
is_intrinsic_call (tree t, enum crug_intrinsic *kind)
{
  if (t == NULL_TREE || TREE_CODE (t) != CALL_EXPR)
    return false;

  tree fn = CALL_EXPR_FN (t);
  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
    fn = TREE_OPERAND (fn, 0);
  if (fn == NULL_TREE)
    return false;

  for (int i = 0; i <= CRUG_INTR_BORROW_MUT; i++)
    if (fn == crug_intr_fndecl[i])
      {
	if (kind != NULL)
	  *kind = (enum crug_intrinsic) i;
	return true;
      }
  return false;
}

/* Owning declarations.  */

void
crug_mark_owning_decl (tree decl, location_t loc)
{
  if (decl == NULL_TREE || decl == error_mark_node || !DECL_P (decl))
    return;

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      error_at (loc, "a Crug qualifier cannot be attached to a typedef "
		"[crug-TYPE-001]");
      return;
    }

  if (DECL_NAME (decl) == NULL_TREE)
    return;

  tree type = TREE_TYPE (decl);
  bool is_param = TREE_CODE (decl) == PARM_DECL;
  bool is_function = TREE_CODE (decl) == FUNCTION_DECL;

  if (type == NULL_TREE || type == error_mark_node)
    return;

  if (is_function)
    {
      /* 'own Foo *f (void)' declares a function that returns an owned
	 pointer: the qualifier belongs to the return type, and the
	 function itself owns nothing.  Recording the declaration lets a
	 later phase check call sites.  */
      crug_owning_uids.insert (DECL_UID (decl));
      return;
    }

  if (TREE_CODE (type) == ARRAY_TYPE)
    {
      /* An array of owned pointers owns its elements.  The Phase 5 MVP
	 tracks the object as one place; the element-wise destruction of an
	 array is a Phase 7 refinement.  */
      type = TREE_TYPE (type);
    }

  if (TREE_CODE (type) != POINTER_TYPE)
    {
      error_at (loc, "an owning declarator must have pointer type "
		"[crug-TYPE-001]");
      return;
    }

  if (TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    {
      error_at (loc, "an owning declarator cannot have pointer-to-function "
		"type [crug-TYPE-005]");
      return;
    }

  (void) is_param;
  (void) is_function;
  crug_owning_uids.insert (DECL_UID (decl));
}

bool
crug_is_owning_decl (tree decl)
{
  return (decl != NULL_TREE && DECL_P (decl) && DECL_NAME (decl) != NULL_TREE
	  && crug_owning_uids.count (DECL_UID (decl)) != 0);
}

void
crug_mark_borrow_decl (tree decl, bool is_mut, location_t loc)
{
  if (decl == NULL_TREE || decl == error_mark_node || !DECL_P (decl))
    return;

  if (TREE_CODE (decl) == TYPE_DECL)
    {
      error_at (loc, "a borrowed declarator cannot be a typedef "
		"[crug-BORROW-007]");
      return;
    }

  if (DECL_NAME (decl) == NULL_TREE)
    return;

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      error_at (loc, "a function cannot return a borrow [crug-BORROW-001]");
      return;
    }

  tree type = TREE_TYPE (decl);
  if (type == NULL_TREE || type == error_mark_node)
    return;

  if (TREE_CODE (type) != POINTER_TYPE
      || TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    {
      error_at (loc, "a borrow qualifier must apply to a pointer declarator "
		"[crug-TYPE-007]");
      return;
    }

  crug_borrow_uids[DECL_UID (decl)] = is_mut;
}

bool
crug_is_borrow_decl (tree decl, bool *is_mut)
{
  if (decl == NULL_TREE || !DECL_P (decl) || DECL_NAME (decl) == NULL_TREE)
    return false;

  std::map<unsigned int, bool>::const_iterator it
    = crug_borrow_uids.find (DECL_UID (decl));
  if (it == crug_borrow_uids.end ())
    return false;

  if (is_mut != NULL)
    *is_mut = it->second;
  return true;
}

/* Dumps.  -fcrug-dump= takes a comma-separated list of kinds.  */

struct crug_dump_slot
{
  const char *kind;
  const char *suffix;
  const char *header;
  FILE *file;
  bool tried;
};

static struct crug_dump_slot crug_dump_slots[] =
{
  { "ownership", ".crug-ownership", "crug ownership dump v1", NULL, false },
  { "drop", ".crug-drop", "crug drop dump v1", NULL, false },
  { "borrow", ".crug-borrow", "crug borrow dump v1", NULL, false }
};

static bool
dump_kind_enabled (const char *kind)
{
  if (crug_dump_kind == NULL)
    return false;

  size_t n = strlen (kind);
  const char *p = crug_dump_kind;
  while (*p != '\0')
    {
      size_t seg = strcspn (p, ",");
      if (seg == n && strncmp (p, kind, n) == 0)
	return true;
      p += seg;
      if (*p == ',')
	p++;
    }
  return false;
}

static FILE *
get_dump_file (const char *kind)
{
  for (size_t i = 0;
       i < sizeof (crug_dump_slots) / sizeof (crug_dump_slots[0]); i++)
    {
      struct crug_dump_slot *slot = &crug_dump_slots[i];
      if (strcmp (slot->kind, kind) != 0)
	continue;
      if (slot->tried)
	return slot->file;
      slot->tried = true;
      if (main_input_filename == NULL)
	return NULL;

      /* Dump next to the object file when the driver gave an output name,
	 so that concurrent compilations do not collide in the current
	 directory.  */
      const char *base = aux_base_name != NULL && aux_base_name[0] != '\0'
	? aux_base_name : lbasename (main_input_filename);
      const char *dot = strrchr (base, '.');
      size_t stem = (dot != NULL && dot != base && strchr (dot, '/') == NULL)
	? (size_t) (dot - base) : strlen (base);
      size_t slen = strlen (slot->suffix);
      char *name = new char[stem + slen + 1];
      memcpy (name, base, stem);
      memcpy (name + stem, slot->suffix, slen + 1);
      slot->file = fopen (name, "w");
      if (slot->file == NULL)
	error ("cannot open the Crug %s dump %qs: %m", kind, name);
      else
	fprintf (slot->file, "%s\n", slot->header);
      delete[] name;
      return slot->file;
    }
  return NULL;
}

static const char *
state_name (crug_state state)
{
  switch (state)
    {
    case CRUG_EMPTY:
      return "EMPTY";
    case CRUG_OWNED:
      return "OWNED";
    case CRUG_MOVED:
      return "MOVED";
    case CRUG_MAYBE_MOVED:
      return "MAYBE_MOVED";
    case CRUG_MAYBE_EMPTY:
      return "MAYBE_EMPTY";
    }
  return "?";
}

static const char *
exit_kind_name (enum crug_exit_kind kind)
{
  switch (kind)
    {
    case CRUG_EXIT_SCOPE_END:
      return "scope-end";
    case CRUG_EXIT_RETURN:
      return "return";
    case CRUG_EXIT_BREAK:
      return "break";
    case CRUG_EXIT_CONTINUE:
      return "continue";
    case CRUG_EXIT_GOTO:
      return "goto";
    }
  return "?";
}

static void
dump_join (location_t loc, tree decl, crug_state a, crug_state b,
	   crug_state result)
{
  if (!dump_kind_enabled ("ownership"))
    return;

  FILE *file = get_dump_file ("ownership");
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

static void
dump_drops (enum crug_exit_kind kind, location_t loc,
	    const std::vector<crug_drop> &drops)
{
  if (drops.empty () || !dump_kind_enabled ("drop"))
    return;

  FILE *file = get_dump_file ("drop");
  if (file == NULL)
    return;

  expanded_location where = expand_location (loc);
  for (size_t i = 0; i < drops.size (); i++)
    fprintf (file, "drop %s:%d: %s %s via %s\n",
	     where.file != NULL ? lbasename (where.file) : "<unknown>",
	     where.line, exit_kind_name (kind),
	     IDENTIFIER_POINTER (DECL_NAME (drops[i].decl)),
	     drops[i].dtor != NULL_TREE
	     && DECL_NAME (drops[i].dtor) != NULL_TREE
	     ? IDENTIFIER_POINTER (DECL_NAME (drops[i].dtor)) : "none");
}

/* A borrow begins at its declaration and ends with the block that declares
   it.  The root lender is printed, so a reborrow is shown against the object
   that is actually borrowed.  */

static void
dump_borrow (bool begin, location_t loc, tree binding, bool is_mut, tree lender)
{
  if (!dump_kind_enabled ("borrow"))
    return;

  FILE *file = get_dump_file ("borrow");
  if (file == NULL || DECL_NAME (binding) == NULL_TREE)
    return;

  expanded_location where = expand_location (loc);
  fprintf (file, "%s %s:%d: %s %s %s\n",
	   begin ? "borrow" : "end",
	   where.file != NULL ? lbasename (where.file) : "<unknown>",
	   where.line,
	   IDENTIFIER_POINTER (DECL_NAME (binding)),
	   is_mut ? "excludes" : "shares",
	   lender != NULL_TREE && DECL_NAME (lender) != NULL_TREE
	   ? IDENTIFIER_POINTER (DECL_NAME (lender)) : "<parameter>");
}

/* The analysis proper.  */

static bool walk_stmt (crug_fn_ctx &ctx, state_map &env, tree t,
		       location_t locus = UNKNOWN_LOCATION);
static void walk_expr (crug_fn_ctx &ctx, state_map &env, tree t,
		       location_t locus = UNKNOWN_LOCATION);

static location_t
node_location (tree t, location_t fallback = UNKNOWN_LOCATION)
{
  if (t == NULL_TREE || t == error_mark_node)
    return fallback;
  if (EXPR_P (t) && EXPR_HAS_LOCATION (t))
    return EXPR_LOCATION (t);
  if (TREE_CODE (t) == BIND_EXPR)
    return node_location (BIND_EXPR_BODY (t), fallback);
  if (TREE_CODE (t) == STATEMENT_LIST)
    {
      tree_stmt_iterator i = tsi_start (t);
      if (!tsi_end_p (i))
	return node_location (tsi_stmt (i), fallback);
    }
  return fallback;
}

static crug_state
state_of (const state_map &env, tree decl)
{
  state_map::const_iterator it = env.find (decl);
  return it == env.end () ? CRUG_EMPTY : it->second.state;
}

static tree
dtor_of (const state_map &env, tree decl)
{
  state_map::const_iterator it = env.find (decl);
  return it == env.end () ? NULL_TREE : it->second.dtor;
}

static void
set_owned (state_map &env, tree decl, tree dtor)
{
  env[decl] = crug_ostate (CRUG_OWNED, dtor);
}

static void
set_state (state_map &env, tree decl, crug_state state)
{
  env[decl] = crug_ostate (state, NULL_TREE);
}

static crug_state
merge_state (crug_state a, crug_state b)
{
  if (a == b)
    return a;
  if ((a == CRUG_EMPTY && b == CRUG_MOVED)
      || (a == CRUG_MOVED && b == CRUG_EMPTY))
    return CRUG_EMPTY;
  if ((a == CRUG_OWNED && b == CRUG_EMPTY)
      || (a == CRUG_EMPTY && b == CRUG_OWNED))
    return CRUG_MAYBE_EMPTY;
  if ((a == CRUG_OWNED && b == CRUG_MOVED)
      || (a == CRUG_MOVED && b == CRUG_OWNED))
    return CRUG_MAYBE_MOVED;
  if ((a == CRUG_EMPTY && b == CRUG_MAYBE_EMPTY)
      || (a == CRUG_MAYBE_EMPTY && b == CRUG_EMPTY))
    return CRUG_MAYBE_EMPTY;
  return CRUG_MAYBE_MOVED;
}

static void
push_frame (crug_fn_ctx &ctx, tree bind)
{
  crug_frame f;
  f.bind = bind;
  ctx.frames.push_back (f);
}

static void
pop_frame (crug_fn_ctx &ctx, state_map &env)
{
  crug_frame &f = ctx.frames.back ();
  for (size_t i = 0; i < f.decls.size (); i++)
    env.erase (f.decls[i]);
  /* A borrow never outlives the block that declares it.  */
  for (size_t i = 0; i < f.borrows.size (); i++)
    {
      std::map<tree, crug_borrow>::iterator b
	= ctx.borrows.find (f.borrows[i]);
      if (b != ctx.borrows.end ())
	{
	  dump_borrow (false, node_location (f.bind), f.borrows[i],
		       b->second.is_mut, b->second.lender);
	  ctx.borrows.erase (b);
	}
      ctx.decl_depth.erase (f.borrows[i]);
    }
  ctx.frames.pop_back ();
}

static void
register_owned (crug_fn_ctx &ctx, state_map &env, tree decl)
{
  crug_frame &f = ctx.frames.back ();
  for (size_t i = 0; i < f.decls.size (); i++)
    if (f.decls[i] == decl)
      return;
  f.decls.push_back (decl);
  ctx.decl_depth[decl] = ctx.frames.size () - 1;
  if (env.find (decl) == env.end ())
    env[decl] = crug_ostate (CRUG_EMPTY, NULL_TREE);
}

/* Record that BINDING holds a borrow of LENDER.  The entry is removed when
   the declaring block ends, so liveness is lexical (ADR 0018 D2; RFC-0001
   section 18.4 asks for last-use analysis in a later phase).  */

static void
register_borrow (crug_fn_ctx &ctx, tree binding, bool is_mut, tree lender)
{
  crug_borrow b;
  b.is_mut = is_mut;
  b.lender = lender;
  b.depth = ctx.frames.size () - 1;
  ctx.frames.back ().borrows.push_back (binding);
  ctx.borrow_declared.insert (binding);
  ctx.decl_depth[binding] = b.depth;
  ctx.borrows[binding] = b;
  dump_borrow (true, DECL_SOURCE_LOCATION (binding), binding, is_mut, lender);
}

/* The object a binding borrows, following reborrows to the root.  */

static tree
resolve_lender (crug_fn_ctx &ctx, tree binding)
{
  std::map<tree, crug_borrow>::const_iterator it = ctx.borrows.find (binding);
  if (it == ctx.borrows.end ())
    return NULL_TREE;
  return it->second.lender;
}

/* True when LENDER has a live borrow; SHARED and MUT report which kinds.  */

static bool
has_live_borrow (crug_fn_ctx &ctx, tree lender, bool *shared, bool *mut)
{
  bool any = false;
  if (shared != NULL)
    *shared = false;
  if (mut != NULL)
    *mut = false;

  for (std::map<tree, crug_borrow>::const_iterator i = ctx.borrows.begin ();
       i != ctx.borrows.end (); ++i)
    {
      if (i->second.lender != lender)
	continue;
      any = true;
      if (i->second.is_mut)
	{
	  if (mut != NULL)
	    *mut = true;
	}
      else if (shared != NULL)
	*shared = true;
    }
  return any;
}

/* The declaration at the base of an lvalue expression: the object whose
   storage an assignment actually writes.  Returns NULL_TREE for anything
   that is not rooted in a local declaration.  */

static tree
expr_base_decl (tree e)
{
  while (e != NULL_TREE)
    {
      switch (TREE_CODE (e))
	{
	case COMPONENT_REF:
	case ARRAY_REF:
	case INDIRECT_REF:
	case MEM_REF:
	case REALPART_EXPR:
	case IMAGPART_EXPR:
	case VIEW_CONVERT_EXPR:
	  e = TREE_OPERAND (e, 0);
	  break;
	case NOP_EXPR:
	case CONVERT_EXPR:
	  if (TREE_CODE (TREE_TYPE (e)) == POINTER_TYPE)
	    return NULL_TREE;
	  e = TREE_OPERAND (e, 0);
	  break;
	default:
	  return (TREE_CODE (e) == VAR_DECL || TREE_CODE (e) == PARM_DECL)
		 ? e : NULL_TREE;
	}
    }
  return NULL_TREE;
}

static tree
choose_dtor (crug_fn_ctx &ctx, tree decl, tree a, tree b, location_t loc)
{
  if (a == b)
    return a;
  if (a == NULL_TREE)
    return b;
  if (b == NULL_TREE)
    return a;
  if (ctx.warned.insert (decl).second)
    error_at (loc, "conflicting destructors for owned value %qD on different "
	      "paths [crug-OWN-010]", decl);
  return a;
}

static void
merge_into (crug_fn_ctx &ctx, state_map &dst, const state_map &other,
	    location_t loc, bool announce = true)
{
  if (dst.empty ())
    {
      dst = other;
      return;
    }

  std::set<tree> keys;
  for (state_map::const_iterator i = dst.begin (); i != dst.end (); ++i)
    keys.insert (i->first);
  for (state_map::const_iterator i = other.begin (); i != other.end (); ++i)
    keys.insert (i->first);

  for (std::set<tree>::const_iterator k = keys.begin (); k != keys.end (); ++k)
    {
      tree d = *k;
      state_map::const_iterator ia = dst.find (d);
      state_map::const_iterator ib = other.find (d);
      crug_ostate sa = ia == dst.end () ? crug_ostate () : ia->second;
      crug_ostate sb = ib == other.end () ? crug_ostate () : ib->second;
      crug_state result = merge_state (sa.state, sb.state);
      if (announce && sa.state != sb.state)
	dump_join (loc, d, sa.state, sb.state, result);
      dst[d] = crug_ostate (result, choose_dtor (ctx, d, sa.dtor, sb.dtor,
						 loc));
    }
}

static void
warn_unadopted (crug_fn_ctx &ctx, tree decl, location_t loc)
{
  (void) loc;
  if (!ctx.warned.insert (decl).second)
    return;
  if (crug_feature_enabled (CRUG_FEAT_STRICT))
    error_at (DECL_SOURCE_LOCATION (decl),
	      "owned value %qD is never adopted or released [crug-OWN-007]",
	      decl);
  else
    warning_at (DECL_SOURCE_LOCATION (decl), OPT_Wcrug_unadopted_own,
		"owned value %qD is never adopted or released [crug-OWN-007]",
		decl);
}

static void
ambiguous_state (crug_fn_ctx &ctx, tree decl, crug_state state,
		 location_t loc)
{
  if (!ctx.warned.insert (decl).second)
    return;
  if (state == CRUG_MAYBE_EMPTY)
    error_at (DECL_SOURCE_LOCATION (decl),
	      "owned value %qD may be used before it is initialized "
	      "[crug-DROP-007]", decl);
  else
    error_at (DECL_SOURCE_LOCATION (decl),
	      "cannot prove that %qD is destroyed exactly once on this path "
	      "[crug-DROP-006]", decl);
  inform (loc, "the ownership state here depends on which branch was taken");
}

/* Collect the destructions for every frame deeper than KEEP_DEPTH, in
   reverse declaration order, and leave those scopes.  */

static void
collect_drops (crug_fn_ctx &ctx, state_map &env, size_t keep_depth,
	       location_t loc, std::vector<crug_drop> &out)
{
  for (size_t i = ctx.frames.size (); i > keep_depth; --i)
    {
      crug_frame &f = ctx.frames[i - 1];
      for (size_t j = f.decls.size (); j > 0; --j)
	{
	  tree d = f.decls[j - 1];
	  state_map::iterator it = env.find (d);
	  if (it == env.end ())
	    continue;

	  crug_ostate st = it->second;
	  switch (st.state)
	    {
	    case CRUG_EMPTY:
	    case CRUG_MOVED:
	      break;
	    case CRUG_OWNED:
	      if (st.dtor != NULL_TREE)
		{
		  crug_drop dr;
		  dr.decl = d;
		  dr.dtor = st.dtor;
		  out.push_back (dr);
		}
	      else
		warn_unadopted (ctx, d, loc);
	      break;
	    case CRUG_MAYBE_MOVED:
	    case CRUG_MAYBE_EMPTY:
	      if (st.dtor != NULL_TREE)
		ambiguous_state (ctx, d, st.state, loc);
	      else
		warn_unadopted (ctx, d, loc);
	      break;
	    }
	  env.erase (it);
	}
    }
}

static void
record_exit (crug_fn_ctx &ctx, state_map &env, tree stmt,
	     enum crug_exit_kind kind, size_t keep_depth, location_t loc,
	     bool has_value)
{
  std::vector<crug_drop> drops;
  collect_drops (ctx, env, keep_depth, loc, drops);
  if (stmt != NULL_TREE && !drops.empty ())
    {
      crug_exit e;
      e.stmt = stmt;
      e.kind = kind;
      e.loc = loc;
      e.drops = drops;
      e.has_value = has_value;
      ctx.exits[stmt] = e;
    }
  dump_drops (kind, loc, drops);
}

/* Allocation catalogue and destructor lookup (ADR 0005, amendment A-3).  */

static bool
is_allocation_call (tree t)
{
  if (t == NULL_TREE || TREE_CODE (t) != CALL_EXPR)
    return false;

  tree fn = CALL_EXPR_FN (t);
  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
    fn = TREE_OPERAND (fn, 0);
  if (fn == NULL_TREE || TREE_CODE (fn) != FUNCTION_DECL
      || DECL_NAME (fn) == NULL_TREE)
    return false;

  const char *name = IDENTIFIER_POINTER (DECL_NAME (fn));
  static const char *const allocs[] =
    {
      "malloc", "calloc", "realloc", "reallocarray", "strdup", "strndup",
      "aligned_alloc", "__builtin_malloc", "__builtin_calloc",
      "__builtin_realloc", "__builtin_strdup", "__builtin_strndup",
      "__builtin_aligned_alloc", NULL
    };
  for (size_t i = 0; allocs[i] != NULL; i++)
    if (strcmp (name, allocs[i]) == 0)
      return true;
  return false;
}

static tree
known_free (void)
{
  static tree cached = NULL_TREE;
  static bool tried = false;

  if (tried)
    return cached;
  tried = true;

  tree id = get_identifier ("free");
  tree decl = lookup_name (id);
  if (decl != NULL_TREE && TREE_CODE (decl) == FUNCTION_DECL)
    cached = decl;
  return cached;
}

static bool
destructor_signature_ok (tree fn, tree optype)
{
  tree fntype = TREE_TYPE (fn);
  if (fntype == NULL_TREE || TREE_CODE (fntype) != FUNCTION_TYPE)
    return false;
  if (!VOID_TYPE_P (TREE_TYPE (fntype)))
    return false;

  tree args = TYPE_ARG_TYPES (fntype);
  if (args == NULL_TREE)
    return true;
  if (TREE_VALUE (args) == void_type_node)
    return false;
  if (TREE_CHAIN (args) != NULL_TREE && TREE_CHAIN (args) != void_list_node)
    return false;

  tree ptype = TREE_VALUE (args);
  if (!POINTER_TYPE_P (ptype) || optype == NULL_TREE
      || !POINTER_TYPE_P (optype))
    return false;
  if (VOID_TYPE_P (TREE_TYPE (ptype)))
    return true;
  return tree_nop_conversion_p (ptype, optype);
}

static void
warn_adopt_signature (crug_fn_ctx &ctx, tree dtor, tree optype, location_t loc)
{
  (void) ctx;
  const char *msg = "adopt() destructor must have type void (*)(T *) for the "
    "operand type T [crug-OWN-002]";
  if (crug_feature_enabled (CRUG_FEAT_STRICT))
    error_at (loc, "%s", msg);
  else
    warning_at (loc, OPT_Wcrug_adopt_signature, "%s", msg);
  (void) dtor;
  (void) optype;
}

static bool
is_null_pointer (tree t)
{
  if (t == NULL_TREE || t == error_mark_node)
    return false;
  if (t == null_pointer_node)
    return true;
  return TREE_CODE (t) == INTEGER_CST && integer_zerop (t);
}

/* Mark an object as moved out of and return the destructor it passed on.  */

static tree
process_move (crug_fn_ctx &ctx, state_map &env, tree arg, location_t loc)
{
  if (arg == NULL_TREE || arg == error_mark_node)
    return NULL_TREE;

  tree r = arg;
  STRIP_NOPS (r);

  if (TREE_CODE (r) == COMPONENT_REF || TREE_CODE (r) == ARRAY_REF)
    {
      error_at (loc, "move of aggregate field %qE is not supported; move the "
		"whole object [crug-UAM-006]", r);
      return NULL_TREE;
    }

  if (TREE_CODE (r) != VAR_DECL && TREE_CODE (r) != PARM_DECL)
    {
      /* A prvalue: there is no object to move out of.  */
      walk_expr (ctx, env, arg, loc);
      return NULL_TREE;
    }

  if (!crug_is_owning_decl (r))
    {
      error_at (loc, "move() operand is not an owned object [crug-OWN-001]");
      return NULL_TREE;
    }

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, r, &shared, &mutborrow))
    error_at (loc, "cannot move %qD while it is borrowed [crug-BORROW-004]", r);

  crug_state st = state_of (env, r);
  tree dtor = dtor_of (env, r);

  if (st == CRUG_MOVED)
    error_at (loc, "move of already-moved value %qD [crug-UAM-002]", r);
  else if (st == CRUG_MAYBE_MOVED)
    error_at (loc, "move of possibly-moved value %qD after a branch join "
	      "[crug-UAM-004]", r);

  env[r] = crug_ostate (CRUG_MOVED, NULL_TREE);
  return dtor;
}

static tree
check_adopt (crug_fn_ctx &ctx, state_map &env, tree call, location_t loc)
{
  tree raw = CALL_EXPR_ARG (call, 0);
  tree dexp = CALL_EXPR_ARG (call, 1);

  walk_expr (ctx, env, raw, loc);

  if (dexp == NULL_TREE || dexp == error_mark_node)
    return NULL_TREE;

  tree d = dexp;
  STRIP_NOPS (d);

  /* A function designator reaches this point without its decay to a pointer
     when it is an argument of an intrinsic, so accept both forms.  */
  tree fn = NULL_TREE;
  if (TREE_CODE (d) == ADDR_EXPR && TREE_CODE (TREE_OPERAND (d, 0)) == FUNCTION_DECL)
    fn = TREE_OPERAND (d, 0);
  else if (TREE_CODE (d) == FUNCTION_DECL)
    fn = d;
  else
    {
      warn_adopt_signature (ctx, d, TREE_TYPE (raw), loc);
      return NULL_TREE;
    }

  if (!destructor_signature_ok (fn, TREE_TYPE (raw)))
    {
      warn_adopt_signature (ctx, fn, TREE_TYPE (raw), loc);
      return NULL_TREE;
    }
  return fn;
}

static void
check_release (crug_fn_ctx &ctx, state_map &env, tree call, location_t loc)
{
  tree arg = CALL_EXPR_ARG (call, 0);
  if (arg == NULL_TREE || arg == error_mark_node)
    return;

  tree r = arg;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) != VAR_DECL && TREE_CODE (r) != PARM_DECL)
      || !crug_is_owning_decl (r))
    {
      error_at (loc, "release() operand is not an owned object [crug-OWN-004]");
      walk_expr (ctx, env, arg, loc);
      return;
    }

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, r, &shared, &mutborrow))
    error_at (loc, "cannot move %qD while it is borrowed [crug-BORROW-004]", r);

  crug_state st = state_of (env, r);
  if (st == CRUG_MOVED)
    error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
  else if (st == CRUG_MAYBE_MOVED)
    error_at (loc, "use of possibly-moved value %qD after a branch join "
	      "[crug-UAM-004]", r);

  env[r] = crug_ostate (CRUG_MOVED, NULL_TREE);
}

static void
check_drop (crug_fn_ctx &ctx, state_map &env, tree call, location_t loc)
{
  tree arg = CALL_EXPR_ARG (call, 0);
  if (arg == NULL_TREE || arg == error_mark_node)
    return;

  tree r = arg;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) != VAR_DECL && TREE_CODE (r) != PARM_DECL)
      || !crug_is_owning_decl (r))
    {
      error_at (loc, "drop() operand is not an owned object [crug-DROP-001]");
      return;
    }

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, r, &shared, &mutborrow))
    error_at (loc, "cannot drop %qD while it is borrowed [crug-BORROW-005]", r);

  crug_state st = state_of (env, r);
  tree dtor = dtor_of (env, r);

  switch (st)
    {
    case CRUG_EMPTY:
      break;
    case CRUG_MOVED:
      error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
      break;
    case CRUG_MAYBE_MOVED:
    case CRUG_MAYBE_EMPTY:
      if (dtor != NULL_TREE)
	ambiguous_state (ctx, r, st, loc);
      else
	warn_unadopted (ctx, r, loc);
      break;
    case CRUG_OWNED:
      if (dtor != NULL_TREE)
	{
	  crug_drop dr;
	  dr.decl = r;
	  dr.dtor = dtor;
	  ctx.explicit_drops[call].push_back (dr);
	}
      else
	warn_unadopted (ctx, r, loc);
      break;
    }

  env[r] = crug_ostate (CRUG_MOVED, NULL_TREE);
}

/* Assign the ownership state that EXPR confers on DECL.  */

static void
assign_from_expr (crug_fn_ctx &ctx, state_map &env, tree decl, tree expr,
		  location_t loc)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    {
      set_state (env, decl, CRUG_EMPTY);
      return;
    }

  tree r = expr;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
      && crug_is_owning_decl (r))
    {
      crug_state st = state_of (env, r);
      if (st == CRUG_MOVED)
	error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
      else if (st == CRUG_MAYBE_MOVED)
	error_at (loc, "use of possibly-moved value %qD after a branch join "
		  "[crug-UAM-004]", r);
      else
	error_at (loc, "implicit copy of owned value %qD; owned values are "
		  "not copyable [crug-UAM-003]", r);
      set_owned (env, decl, NULL_TREE);
      return;
    }

  enum crug_intrinsic kind;
  if (TREE_CODE (r) == CALL_EXPR && is_intrinsic_call (r, &kind))
    {
      if (kind == CRUG_INTR_MOVE)
	{
	  set_owned (env, decl, process_move (ctx, env, CALL_EXPR_ARG (r, 0),
					      loc));
	  return;
	}
      if (kind == CRUG_INTR_ADOPT)
	{
	  set_owned (env, decl, check_adopt (ctx, env, r, loc));
	  return;
	}
      walk_expr (ctx, env, expr, loc);
      set_owned (env, decl, NULL_TREE);
      return;
    }

  if (is_null_pointer (r))
    {
      set_state (env, decl, CRUG_EMPTY);
      return;
    }

  if (is_allocation_call (r))
    {
      walk_expr (ctx, env, expr, loc);
      set_owned (env, decl, known_free ());
      return;
    }

  walk_expr (ctx, env, expr, loc);
  set_owned (env, decl, NULL_TREE);
}

/* A borrow must not outlive the object it borrows.  Both declarations are
   in frames of the function, and a binding declared in a shallower frame
   than its lender would still be live after the lender is destroyed.  */

static void
borrow_escape_check (crug_fn_ctx &ctx, tree binding, tree lender,
		     location_t loc)
{
  std::map<tree, size_t>::const_iterator me
    = ctx.decl_depth.find (binding);
  std::map<tree, size_t>::const_iterator le = ctx.decl_depth.find (lender);
  if (me == ctx.decl_depth.end () || le == ctx.decl_depth.end ())
    return;
  if (me->second < le->second)
    error_at (loc, "borrow %qD escapes the lifetime of its referent "
	      "[crug-BORROW-007]", binding);
}

/* Validate the operand of borrow(e) / borrow_mut(e) and return the root
   object it borrows, or NULL_TREE after reporting.  Reading the operand is
   not a use: the checks below read the ownership state directly, so a call
   does not also report crug-BORROW-015 against its own lender.  */

static tree
check_borrow (crug_fn_ctx &ctx, state_map &env, tree call, bool want_mut,
	      location_t loc)
{
  const char *what = want_mut ? "borrow_mut()" : "borrow()";
  tree arg = CALL_EXPR_ARG (call, 0);
  if (arg == NULL_TREE || arg == error_mark_node)
    return NULL_TREE;

  tree r = arg;
  STRIP_NOPS (r);

  if (TREE_CODE (r) != VAR_DECL && TREE_CODE (r) != PARM_DECL)
    {
      if (TREE_CODE (r) == COMPONENT_REF || TREE_CODE (r) == ARRAY_REF
	  || TREE_CODE (r) == INDIRECT_REF || TREE_CODE (r) == MEM_REF)
	error_at (loc, "%s operand must be a whole owned object or a borrow; "
		  "member borrows are not supported in this phase "
		  "[crug-BORROW-009]", what);
      else
	error_at (loc, "%s operand must be an lvalue [crug-BORROW-006]", what);
      return NULL_TREE;
    }

  bool arg_mut = false;
  if (crug_is_borrow_decl (r, &arg_mut))
    {
      if (want_mut)
	{
	  error_at (loc, "a mutable borrow of a borrow is not supported in "
		    "this phase [crug-BORROW-010]");
	  return NULL_TREE;
	}
      if (arg_mut)
	{
	  error_at (loc, "cannot borrow %qD while it is mutably borrowed "
		    "[crug-BORROW-016]", r);
	  return NULL_TREE;
	}
      tree root = resolve_lender (ctx, r);
      return root != NULL_TREE ? root : r;
    }

  if (!crug_is_owning_decl (r))
    {
      if (want_mut)
	error_at (loc, "%s operand must be a whole modifiable owned object "
		  "[crug-BORROW-010]", what);
      else
	error_at (loc, "%s operand must be a whole owned object or a borrow; "
		  "member borrows are not supported in this phase "
		  "[crug-BORROW-009]", what);
      return NULL_TREE;
    }

  crug_state st = state_of (env, r);
  if (st == CRUG_EMPTY)
    {
      error_at (loc, "cannot borrow %qD because it owns nothing "
		"[crug-BORROW-011]", r);
      return NULL_TREE;
    }
  if (st == CRUG_MOVED)
    {
      error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
      return NULL_TREE;
    }
  if (st == CRUG_MAYBE_MOVED || st == CRUG_MAYBE_EMPTY)
    {
      error_at (loc, "use of possibly-moved value %qD after a branch join "
		"[crug-UAM-004]", r);
      return NULL_TREE;
    }

  bool shared = false;
  bool mutborrow = false;
  (void) has_live_borrow (ctx, r, &shared, &mutborrow);
  if (want_mut)
    {
      if (mutborrow)
	{
	  error_at (loc, "cannot mutably borrow %qD more than once "
		    "[crug-BORROW-003]", r);
	  return NULL_TREE;
	}
      if (shared)
	{
	  error_at (loc, "cannot mutably borrow %qD while it is shared-borrowed "
		    "[crug-BORROW-002]", r);
	  return NULL_TREE;
	}
    }
  else if (mutborrow)
    {
      error_at (loc, "cannot borrow %qD while it is mutably borrowed "
		"[crug-BORROW-016]", r);
      return NULL_TREE;
    }

  return r;
}

/* Initialize a borrowed binding from EXPR (ADR 0018 D3 R3, RFC-0001
   section 18).  A borrow that outlives the block of its lender is rejected.  */

static void
assign_borrow (crug_fn_ctx &ctx, state_map &env, tree decl, tree expr,
	       location_t loc, bool want_mut)
{
  if (expr == NULL_TREE || expr == error_mark_node)
    return;

  tree r = expr;
  STRIP_NOPS (r);

  if (TREE_CODE (r) == CALL_EXPR)
    {
      enum crug_intrinsic kind;
      if (is_intrinsic_call (r, &kind)
	  && (kind == CRUG_INTR_BORROW || kind == CRUG_INTR_BORROW_MUT))
	{
	  bool src_mut = kind == CRUG_INTR_BORROW_MUT;
	  if (want_mut && !src_mut)
	    {
	      error_at (loc, "a mutable borrow cannot be initialized from "
			"%<borrow()%> [crug-BORROW-012]");
	      return;
	    }
	  tree root = check_borrow (ctx, env, r, src_mut, loc);
	  if (root == NULL_TREE)
	    return;
	  borrow_escape_check (ctx, decl, root, loc);
	  register_borrow (ctx, decl, src_mut, root);
	  return;
	}
    }

  bool src_mut = false;
  if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
      && crug_is_borrow_decl (r, &src_mut))
    {
      if (TREE_CODE (r) == VAR_DECL
	  && ctx.borrows.find (r) == ctx.borrows.end ())
	{
	  error_at (loc, "borrow %qD is used without being initialized on "
		    "this path [crug-BORROW-013]", r);
	  return;
	}
      if (want_mut && !src_mut)
	{
	  error_at (loc, "a mutable borrow cannot be initialized from a "
		    "shared borrow [crug-BORROW-012]");
	  return;
	}
      if (!want_mut && src_mut)
	{
	  error_at (loc, "cannot borrow %qD while it is mutably borrowed "
		    "[crug-BORROW-016]", r);
	  return;
	}
      tree root = resolve_lender (ctx, r);
      if (root == NULL_TREE)
	root = r;
      borrow_escape_check (ctx, decl, root, loc);
      register_borrow (ctx, decl, src_mut, root);
      return;
    }

  error_at (loc, "borrow %qD must be initialized from %<borrow()%>, "
	    "%<borrow_mut()%> or another borrow [crug-BORROW-017]", decl);
}

static tree expr_base_decl (tree e);

static void
check_use (crug_fn_ctx &ctx, state_map &env, tree decl, location_t loc)
{
  bool is_mut = false;
  if (crug_is_borrow_decl (decl, &is_mut))
    {
      /* A borrow that was declared but never initialized on this path has
	 no lender to conflict with, and reading it would read a dangling
	 pointer.  */
      if (TREE_CODE (decl) == VAR_DECL
	  && DECL_INITIAL (decl) == NULL_TREE
	  && ctx.borrows.find (decl) == ctx.borrows.end ())
	error_at (loc, "borrow %qD is used without being initialized on this "
		  "path [crug-BORROW-013]", decl);
      return;
    }

  if (!crug_is_owning_decl (decl))
    return;

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, decl, &shared, &mutborrow) && mutborrow)
    error_at (loc, "cannot use %qD while it is mutably borrowed "
	      "[crug-BORROW-015]", decl);

  crug_state state = state_of (env, decl);
  if (state == CRUG_MOVED)
    error_at (loc, "use of moved value %qD [crug-UAM-001]", decl);
  else if (state == CRUG_MAYBE_MOVED)
    error_at (loc, "use of possibly-moved value %qD after a branch join "
	      "[crug-UAM-004]", decl);
}

/* setjmp and longjmp do not run the destructors of the frames they skip.
   Warn once per owning local that is live and has a destructor when the
   setjmp call is evaluated: a later longjmp may bypass its drop.  */

static bool
is_setjmp_call (tree t)
{
  tree fn = CALL_EXPR_FN (t);
  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
    fn = TREE_OPERAND (fn, 0);
  if (fn == NULL_TREE || TREE_CODE (fn) != FUNCTION_DECL
      || DECL_NAME (fn) == NULL_TREE)
    return false;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fn));
  return strcmp (name, "setjmp") == 0
	 || strcmp (name, "_setjmp") == 0
	 || strcmp (name, "sigsetjmp") == 0
	 || strcmp (name, "__sigsetjmp") == 0;
}

static void
warn_setjmp (crug_fn_ctx &ctx, state_map &env, location_t loc)
{
  for (state_map::const_iterator i = env.begin (); i != env.end (); ++i)
    if (i->second.state == CRUG_OWNED && i->second.dtor != NULL_TREE)
      {
	tree d = i->first;
	if (!ctx.warned_setjmp.insert (d).second)
	  continue;
	if (crug_feature_enabled (CRUG_FEAT_STRICT))
	  error_at (loc, "destructors between %<setjmp%> and %<longjmp%> may "
		    "be skipped for %qD [crug-DROP-003]", d);
	else
	  warning_at (loc, OPT_Wcrug_setjmp_drops,
		      "destructors between %<setjmp%> and %<longjmp%> may be "
		      "skipped for %qD [crug-DROP-003]", d);
      }
}

static void
walk_expr (crug_fn_ctx &ctx, state_map &env, tree t, location_t locus)
{
  if (t == NULL_TREE || t == error_mark_node)
    return;

  location_t loc = node_location (t, locus);

  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
      check_use (ctx, env, t, loc);
      return;

    case CALL_EXPR:
      {
	enum crug_intrinsic kind;
	if (is_intrinsic_call (t, &kind))
	  {
	    switch (kind)
	      {
	      case CRUG_INTR_MOVE:
		(void) process_move (ctx, env, CALL_EXPR_ARG (t, 0), loc);
		return;
	      case CRUG_INTR_ADOPT:
		(void) check_adopt (ctx, env, t, loc);
		return;
	      case CRUG_INTR_RELEASE:
		check_release (ctx, env, t, loc);
		return;
	      case CRUG_INTR_DROP:
		check_drop (ctx, env, t, loc);
		return;
	      case CRUG_INTR_BORROW:
	      case CRUG_INTR_BORROW_MUT:
		/* A borrow used without a binding ends immediately.  */
		(void) check_borrow (ctx, env, t,
			     kind == CRUG_INTR_BORROW_MUT, loc);
		return;
	      }
	  }
	if (is_setjmp_call (t))
	  warn_setjmp (ctx, env, loc);
	walk_expr (ctx, env, CALL_EXPR_FN (t), loc);
	for (int i = 0; i < call_expr_nargs (t); i++)
	  walk_expr (ctx, env, CALL_EXPR_ARG (t, i), loc);
	return;
      }

    case MODIFY_EXPR:
      {
	tree lhs = TREE_OPERAND (t, 0);
	tree rhs = TREE_OPERAND (t, 1);
	bool lhs_mut = false;
	if (TREE_CODE (lhs) == VAR_DECL && crug_is_borrow_decl (lhs, &lhs_mut))
	  {
	    /* Re-initializing a borrow ends the previous one here.  */
	    std::map<tree, crug_borrow>::iterator prev
	      = ctx.borrows.find (lhs);
	    if (prev != ctx.borrows.end ())
	      ctx.borrows.erase (prev);
	    assign_borrow (ctx, env, lhs, rhs, loc, lhs_mut);
	    return;
	  }
	tree base = expr_base_decl (lhs);
	bool base_mut = false;
	if (base != NULL_TREE && crug_is_borrow_decl (base, &base_mut))
	  {
	    if (!base_mut)
	      error_at (loc, "assignment through shared borrow %qD "
			"[crug-BORROW-008]", base);
	    /* Writing through a borrow does not change what the owner owns,
	       and the borrow binding itself is not assigned.  */
	    walk_expr (ctx, env, lhs, loc);
	    walk_expr (ctx, env, rhs, loc);
	    return;
	  }
	if ((TREE_CODE (lhs) == VAR_DECL || TREE_CODE (lhs) == PARM_DECL)
	    && crug_is_owning_decl (lhs))
	  {
	    bool shared = false;
	    bool mutborrow = false;
	    if (has_live_borrow (ctx, lhs, &shared, &mutborrow))
	      {
		error_at (loc, "cannot assign to %qD while it is borrowed "
			  "[crug-BORROW-014]", lhs);
		walk_expr (ctx, env, rhs, loc);
		return;
	      }
	    crug_state st = state_of (env, lhs);
	    if (st == CRUG_MOVED)
	      error_at (loc, "use of moved value %qD [crug-UAM-001]", lhs);
	    else if (st == CRUG_MAYBE_MOVED)
	      error_at (loc, "use of possibly-moved value %qD after a branch "
			"join [crug-UAM-004]", lhs);
	    else if (st == CRUG_OWNED || st == CRUG_MAYBE_EMPTY)
	      {
		/* Assigning to an object that already owns a value would
		   overwrite it without destroying it.  */
		error_at (loc, "assignment to an owned value that already owns; "
			  "call drop() or release() first [crug-DROP-008]", lhs);
		/* Recovery: the value is not destroyed here, so stop calling
		   it owned and do not model the assignment.  This keeps one
		   mistake from producing a second, unrelated diagnostic at
		   the end of the scope.  */
		crug_ostate &s = env[lhs];
		s.state = CRUG_MOVED;
		s.dtor = NULL_TREE;
		walk_expr (ctx, env, rhs, loc);
		return;
	      }
	    assign_from_expr (ctx, env, lhs, rhs, loc);
	  }
	else
	  {
	    walk_expr (ctx, env, lhs, loc);
	    walk_expr (ctx, env, rhs, loc);
	  }
	return;
      }

    case TARGET_EXPR:
    case COMPOUND_EXPR:
    case COND_EXPR:
    case TRUTH_ANDIF_EXPR:
    case TRUTH_ORIF_EXPR:
    case TRUTH_NOT_EXPR:
      /* These carry statements or short-circuit control flow; walking the
	 operands in order is enough for the ownership state.  */
      break;

    default:
      break;
    }

  if (!EXPR_P (t))
    return;

  for (int i = 0; i < TREE_OPERAND_LENGTH (t); i++)
    walk_expr (ctx, env, TREE_OPERAND (t, i), loc);
}

/* Record the label positions and the owning declarations textually before
   them, for goto validation.  */

static void
collect_labels (tree t, std::vector<tree> &path, std::vector<tree> &decls,
		std::map<tree, crug_label_info> &labels)
{
  if (t == NULL_TREE || t == error_mark_node)
    return;

  switch (TREE_CODE (t))
    {
    case BIND_EXPR:
      {
	size_t mark = decls.size ();
	path.push_back (t);
	collect_labels (BIND_EXPR_BODY (t), path, decls, labels);
	path.pop_back ();
	decls.resize (mark);
	return;
      }

    case STATEMENT_LIST:
      for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	collect_labels (tsi_stmt (i), path, decls, labels);
      return;

    case DECL_EXPR:
      {
	tree d = DECL_EXPR_DECL (t);
	if (d != NULL_TREE && TREE_CODE (d) == VAR_DECL
	    && crug_is_owning_decl (d))
	  decls.push_back (d);
	return;
      }

    case LABEL_EXPR:
      {
	crug_label_info info;
	info.path = path;
	info.decls_before = decls;
	labels[LABEL_EXPR_LABEL (t)] = info;
	return;
      }

    case COND_EXPR:
      collect_labels (TREE_OPERAND (t, 1), path, decls, labels);
      collect_labels (TREE_OPERAND (t, 2), path, decls, labels);
      return;

    case FOR_STMT:
      if (FOR_INIT_STMT (t) != NULL_TREE)
	collect_labels (FOR_INIT_STMT (t), path, decls, labels);
      collect_labels (FOR_BODY (t), path, decls, labels);
      return;

    case WHILE_STMT:
      collect_labels (WHILE_BODY (t), path, decls, labels);
      return;

    case DO_STMT:
      collect_labels (DO_BODY (t), path, decls, labels);
      return;

    case SWITCH_STMT:
      collect_labels (SWITCH_STMT_BODY (t), path, decls, labels);
      return;

    default:
      return;
    }
}

static void
check_gotos (crug_fn_ctx &ctx)
{
  for (size_t g = 0; g < ctx.gotos.size (); g++)
    {
      crug_goto &go = ctx.gotos[g];
      std::map<tree, state_map>::iterator le = ctx.label_envs.find (go.label);
      if (le == ctx.label_envs.end ())
	continue;

      for (state_map::iterator i = go.env.begin (); i != go.env.end (); ++i)
	{
	  tree d = i->first;
	  state_map::iterator j = le->second.find (d);
	  if (j == le->second.end ())
	    {
	      if (ctx.warned.insert (d).second)
		error_at (go.loc, "goto jumps into the scope of owned %qD "
			  "[crug-DROP-002]", d);
	      continue;
	    }
	  if (i->second.state != j->second.state
	      && ctx.warned.insert (d).second)
	    error_at (go.loc, "cannot prove that %qD is destroyed exactly once "
		      "on this path [crug-DROP-006]", d);
	}

      for (state_map::iterator j = le->second.begin ();
	   j != le->second.end (); ++j)
	if (go.env.find (j->first) == go.env.end ()
	    && ctx.warned.insert (j->first).second)
	  error_at (go.loc, "goto jumps into the scope of owned %qD "
		    "[crug-DROP-002]", j->first);
    }
}

static bool
walk_stmt (crug_fn_ctx &ctx, state_map &env, tree t, location_t locus)
{
  if (t == NULL_TREE || t == error_mark_node)
    return true;

  location_t loc = node_location (t, locus);

  switch (TREE_CODE (t))
    {
    case BIND_EXPR:
      {
	size_t depth = ctx.frames.size ();
	push_frame (ctx, t);
	bool reach = walk_stmt (ctx, env, BIND_EXPR_BODY (t), loc);
	if (reach)
	  {
	    std::vector<crug_drop> drops;
	    collect_drops (ctx, env, depth, loc, drops);
	    if (!drops.empty ())
	      ctx.scope_drops[t] = drops;
	    dump_drops (CRUG_EXIT_SCOPE_END, loc, drops);
	  }
	pop_frame (ctx, env);
	return reach;
      }

    case STATEMENT_LIST:
      {
	bool reach = true;
	for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	  {
	    tree stmt = tsi_stmt (i);
	    if (!reach
		&& (TREE_CODE (stmt) == LABEL_EXPR
		    || TREE_CODE (stmt) == CASE_LABEL_EXPR))
	      reach = true;
	    if (!reach)
	      continue;
	    reach = walk_stmt (ctx, env, stmt, loc);
	  }
	return reach;
      }

    case DECL_EXPR:
      {
	tree decl = DECL_EXPR_DECL (t);
	bool decl_is_mut = false;
	if (decl != NULL_TREE && TREE_CODE (decl) == VAR_DECL
	    && crug_is_borrow_decl (decl, &decl_is_mut))
	  {
	    /* The binding exists from here on, even if it is not initialized
	       yet: its frame decides how long the borrow lives.  */
	    ctx.frames.back ().borrows.push_back (decl);
	    ctx.borrow_declared.insert (decl);
	    ctx.decl_depth[decl] = ctx.frames.size () - 1;
	    assign_borrow (ctx, env, decl, DECL_INITIAL (decl), loc,
			   decl_is_mut);
	    return true;
	  }
	if (decl == NULL_TREE || TREE_CODE (decl) != VAR_DECL)
	  return true;

	if (!crug_is_owning_decl (decl))
	  {
	    /* An ordinary variable's initializer is still evaluated: it can
	       use a moved value, call move(), or take a borrow.  */
	    if (DECL_INITIAL (decl) != NULL_TREE)
	      walk_expr (ctx, env, DECL_INITIAL (decl), loc);
	    return true;
	  }

	register_owned (ctx, env, decl);
	assign_from_expr (ctx, env, decl, DECL_INITIAL (decl), loc);
	return true;
      }

    case RETURN_EXPR:
      {
	bool has_value = TREE_OPERAND (t, 0) != NULL_TREE;
	if (has_value)
	  walk_expr (ctx, env, TREE_OPERAND (t, 0), loc);
	record_exit (ctx, env, t, CRUG_EXIT_RETURN, 0, loc, has_value);
	return false;
      }

    case BREAK_STMT:
    case CONTINUE_STMT:
      {
	bool is_break = TREE_CODE (t) == BREAK_STMT;
	size_t found = (size_t) -1;
	for (size_t i = ctx.targets.size (); i > 0; --i)
	  {
	    if (is_break || ctx.targets[i - 1].is_loop)
	      {
		found = i - 1;
		break;
	      }
	  }
	if (found == (size_t) -1)
	  return false;

	record_exit (ctx, env, t,
		     is_break ? CRUG_EXIT_BREAK : CRUG_EXIT_CONTINUE,
		     ctx.targets[found].keep_depth, loc, false);
	ctx.targets[found].edges.push_back (env);
	return false;
      }

    case GOTO_EXPR:
      {
	tree label = GOTO_DESTINATION (t);
	std::map<tree, crug_label_info>::iterator li = ctx.labels.find (label);
	if (li == ctx.labels.end ())
	  return false;

	const crug_label_info &info = li->second;
	size_t common = 0;
	while (common < ctx.frames.size () && common < info.path.size ()
	       && ctx.frames[common].bind == info.path[common])
	  common++;

	if (common < info.path.size ())
	  {
	    /* The label lives in a block that this goto does not enter.  Any
	       owning object declared textually before the label in that block
	       is entered without being initialized.  */
	    for (size_t i = 0; i < info.decls_before.size (); i++)
	      if (env.find (info.decls_before[i]) == env.end ()
		  && ctx.warned.insert (info.decls_before[i]).second)
		error_at (loc, "goto jumps into the scope of owned %qD "
			  "[crug-DROP-002]", info.decls_before[i]);
	  }

	record_exit (ctx, env, t, CRUG_EXIT_GOTO, common, loc, false);

	crug_goto go;
	go.stmt = t;
	go.label = label;
	go.loc = loc;
	go.env = env;
	ctx.gotos.push_back (go);
	return false;
      }

    case LABEL_EXPR:
      merge_into (ctx, ctx.label_envs[LABEL_EXPR_LABEL (t)], env, loc, false);
      return true;

    case CASE_LABEL_EXPR:
      for (size_t i = ctx.targets.size (); i > 0; --i)
	if (!ctx.targets[i - 1].is_loop)
	  {
	    merge_into (ctx, ctx.targets[i - 1].cases, env, loc, false);
	    break;
	  }
      return true;

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
	bool then_reach = walk_stmt (ctx, then_env, TREE_OPERAND (t, 1), loc);
	bool else_reach = walk_stmt (ctx, else_env, TREE_OPERAND (t, 2), loc);

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
	  walk_stmt (ctx, env, FOR_INIT_STMT (t), loc);
	if (FOR_COND (t) != NULL_TREE)
	  walk_expr (ctx, env, FOR_COND (t), loc);

	crug_target target;
	target.is_loop = true;
	target.keep_depth = ctx.frames.size ();
	ctx.targets.push_back (target);

	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, FOR_BODY (t), loc);
	if (FOR_EXPR (t) != NULL_TREE)
	  walk_expr (ctx, body, FOR_EXPR (t), loc);

	crug_target done = ctx.targets.back ();
	ctx.targets.pop_back ();

	state_map merged = env;
	if (body_reach)
	  merge_into (ctx, merged, body, loc);
	for (size_t i = 0; i < done.edges.size (); i++)
	  merge_into (ctx, merged, done.edges[i], loc);
	env = merged;
	return true;
      }

    case WHILE_STMT:
      {
	if (WHILE_COND (t) != NULL_TREE)
	  walk_expr (ctx, env, WHILE_COND (t), loc);

	crug_target target;
	target.is_loop = true;
	target.keep_depth = ctx.frames.size ();
	ctx.targets.push_back (target);

	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, WHILE_BODY (t), loc);

	crug_target done = ctx.targets.back ();
	ctx.targets.pop_back ();

	state_map merged = env;
	if (body_reach)
	  merge_into (ctx, merged, body, loc);
	for (size_t i = 0; i < done.edges.size (); i++)
	  merge_into (ctx, merged, done.edges[i], loc);
	env = merged;
	return true;
      }

    case DO_STMT:
      {
	crug_target target;
	target.is_loop = true;
	target.keep_depth = ctx.frames.size ();
	ctx.targets.push_back (target);

	state_map body = env;
	bool body_reach = walk_stmt (ctx, body, DO_BODY (t), loc);
	if (DO_COND (t) != NULL_TREE)
	  walk_expr (ctx, body, DO_COND (t), loc);

	crug_target done = ctx.targets.back ();
	ctx.targets.pop_back ();

	state_map merged = body;
	for (size_t i = 0; i < done.edges.size (); i++)
	  merge_into (ctx, merged, done.edges[i], loc);
	env = merged;
	return body_reach;
      }

    case SWITCH_STMT:
      {
	if (SWITCH_STMT_COND (t) != NULL_TREE)
	  walk_expr (ctx, env, SWITCH_STMT_COND (t), loc);

	crug_target target;
	target.is_loop = false;
	target.keep_depth = ctx.frames.size ();
	ctx.targets.push_back (target);

	bool body_reach = walk_stmt (ctx, env, SWITCH_STMT_BODY (t), loc);

	crug_target done = ctx.targets.back ();
	ctx.targets.pop_back ();

	state_map merged = env;
	if (body_reach)
	  merge_into (ctx, merged, env, loc);
	for (size_t i = 0; i < done.edges.size (); i++)
	  merge_into (ctx, merged, done.edges[i], loc);
	/* The case labels that the switch can actually enter.  */
	bool have_cases = !done.cases.empty ();
	for (state_map::iterator i = done.cases.begin (); i != done.cases.end ();
	     ++i)
	  {
	    if (merged.find (i->first) == merged.end ())
	      merged[i->first] = i->second;
	    else
	      {
		crug_ostate a = merged[i->first];
		crug_state r = merge_state (a.state, i->second.state);
		merged[i->first] = crug_ostate (r,
						choose_dtor (ctx, i->first,
							     a.dtor,
							     i->second.dtor, loc));
	      }
	  }
	(void) have_cases;
	env = merged;
	return true;
      }

    case ASM_EXPR:
      return true;

    default:
      if (EXPR_P (t))
	{
	  enum crug_intrinsic kind;
	  if (TREE_CODE (t) == CALL_EXPR && is_intrinsic_call (t, &kind))
	    {
	      walk_expr (ctx, env, t, loc);
	      if (kind != CRUG_INTR_DROP)
		ctx.stmt_replace[t] = build1 (NOP_EXPR, void_type_node,
					      CALL_EXPR_ARG (t, 0));
	    }
	  else
	    walk_expr (ctx, env, t, loc);
	}
      return true;
    }
}

/* Pass B: emit the destructions.  */

static tree
drop_call_stmt (tree decl, tree dtor, location_t loc)
{
  /* The destructor takes a pointer to the owned object: for
     'own Foo *p' that is the value of p, not the address of p.  */
  tree fntype = TREE_TYPE (dtor);
  tree argtype = NULL_TREE;
  if (fntype != NULL_TREE && TREE_CODE (fntype) == FUNCTION_TYPE
      && TYPE_ARG_TYPES (fntype) != NULL_TREE)
    argtype = TREE_VALUE (TYPE_ARG_TYPES (fntype));

  tree arg = decl;
  if (argtype != NULL_TREE && argtype != void_type_node
      && argtype != TREE_TYPE (decl))
    arg = fold_convert_loc (loc, argtype, decl);

  tree call = build_call_expr_loc (loc, dtor, 1, arg);
  tree test = build2 (NE_EXPR, boolean_type_node, decl, null_pointer_node);
  tree stmt = build3 (COND_EXPR, void_type_node, test, call, void_node);
  SET_EXPR_LOCATION (stmt, loc);
  return stmt;
}

static void
append_drops (tree &list, const std::vector<crug_drop> &drops, location_t loc)
{
  for (size_t i = 0; i < drops.size (); i++)
    append_to_statement_list (drop_call_stmt (drops[i].decl, drops[i].dtor,
					      loc), &list);
}

static tree
wrap_exit (crug_fn_ctx &ctx, tree stmt, const crug_exit &e)
{
  tree list = NULL_TREE;

  if (e.kind == CRUG_EXIT_RETURN && e.has_value)
    {
      tree res = DECL_RESULT (ctx.fndecl);
      tree op0 = TREE_OPERAND (stmt, 0);
      tree value = op0;
      if (op0 != NULL_TREE && TREE_CODE (op0) == MODIFY_EXPR)
	value = TREE_OPERAND (op0, 1);

      tree restype = TREE_TYPE (res);
      if (TREE_CODE (restype) == REFERENCE_TYPE
	  && TREE_TYPE (restype) != NULL_TREE)
	restype = TREE_TYPE (restype);

      tree tmp = build_decl (e.loc, VAR_DECL, NULL_TREE, restype);
      DECL_ARTIFICIAL (tmp) = 1;
      DECL_IGNORED_P (tmp) = 1;
      DECL_CONTEXT (tmp) = ctx.fndecl;
      /* BIND_EXPR_VARS is a chain of declarations linked with DECL_CHAIN.  */
      DECL_CHAIN (tmp) = BIND_EXPR_VARS (ctx.body);
      BIND_EXPR_VARS (ctx.body) = tmp;

      if (value != NULL_TREE && TREE_TYPE (value) != restype)
	value = fold_convert_loc (e.loc, restype, value);

      tree assign = build2 (MODIFY_EXPR, restype, tmp, value);
      SET_EXPR_LOCATION (assign, e.loc);
      append_to_statement_list (assign, &list);
      append_drops (list, e.drops, e.loc);

      tree resassign = build2 (MODIFY_EXPR, TREE_TYPE (res), res, tmp);
      SET_EXPR_LOCATION (resassign, e.loc);
      append_to_statement_list (build_stmt (e.loc, RETURN_EXPR, resassign),
				&list);
    }
  else
    {
      append_drops (list, e.drops, e.loc);
      append_to_statement_list (stmt, &list);
    }

  tree block = build_block (NULL_TREE, NULL_TREE, DECL_INITIAL (ctx.fndecl),
			    NULL_TREE);
  tree bind = build3 (BIND_EXPR, void_type_node, NULL_TREE, list, block);
  SET_EXPR_LOCATION (bind, e.loc);
  return bind;
}

static tree
rewrite_cb (tree *tp, int *walk_subtrees, void *data)
{
  crug_fn_ctx *ctx = (crug_fn_ctx *) data;

  if (*tp == NULL_TREE)
    return NULL_TREE;

  if (TREE_CODE (*tp) == BIND_EXPR)
    {
      std::map<tree, std::vector<crug_drop> >::iterator it
	= ctx->scope_drops.find (*tp);
      if (it != ctx->scope_drops.end ())
	{
	  tree list = NULL_TREE;
	  append_to_statement_list (BIND_EXPR_BODY (*tp), &list);
	  append_drops (list, it->second, node_location (*tp));
	  BIND_EXPR_BODY (*tp) = list;
	  ctx->scope_drops.erase (it);
	}
      return NULL_TREE;
    }

  if (TREE_CODE (*tp) == CALL_EXPR)
    {
      std::map<tree, tree>::iterator sr = ctx->stmt_replace.find (*tp);
      if (sr != ctx->stmt_replace.end ())
	{
	  tree repl = sr->second;
	  ctx->stmt_replace.erase (sr);
	  *tp = repl;
	  *walk_subtrees = 0;
	  return NULL_TREE;
	}

      std::map<tree, std::vector<crug_drop> >::iterator ed
	= ctx->explicit_drops.find (*tp);
      if (ed != ctx->explicit_drops.end ())
	{
	  tree seq = NULL_TREE;
	  for (size_t i = 0; i < ed->second.size (); i++)
	    {
	      tree stmt = drop_call_stmt (ed->second[i].decl,
					  ed->second[i].dtor,
					  node_location (*tp));
	      seq = seq == NULL_TREE ? stmt
		: build2 (COMPOUND_EXPR, void_type_node, seq, stmt);
	    }
	  if (seq == NULL_TREE)
	    seq = build_empty_stmt (node_location (*tp));
	  ctx->explicit_drops.erase (ed);
	  *tp = seq;
	  *walk_subtrees = 0;
	  return NULL_TREE;
	}

      enum crug_intrinsic kind;
      if (is_intrinsic_call (*tp, &kind) && kind != CRUG_INTR_DROP)
	{
	  *tp = CALL_EXPR_ARG (*tp, 0);
	  *walk_subtrees = 1;
	  return NULL_TREE;
	}
    }

  std::map<tree, crug_exit>::iterator it = ctx->exits.find (*tp);
  if (it != ctx->exits.end ())
    {
      crug_exit e = it->second;
      ctx->exits.erase (it);
      *tp = wrap_exit (*ctx, *tp, e);
      *walk_subtrees = 1;
      return NULL_TREE;
    }

  return NULL_TREE;
}

static tree
lower_intrinsic_cb (tree *tp, int *walk_subtrees, void *data)
{
  (void) data;
  enum crug_intrinsic kind;
  if (is_intrinsic_call (*tp, &kind))
    {
      if (kind == CRUG_INTR_DROP)
	*tp = build_empty_stmt (node_location (*tp));
      else
	*tp = CALL_EXPR_ARG (*tp, 0);
      *walk_subtrees = 1;
    }
  return NULL_TREE;
}

void
crug_check_function (tree fndecl, tree body)
{
  if (fndecl == NULL_TREE || body == NULL_TREE)
    return;
  if (!crug_ownership_enabled () && !crug_borrow_enabled ())
    return;

  crug_fn_ctx ctx;
  ctx.fndecl = fndecl;
  ctx.body = body;

  std::vector<tree> path;
  std::vector<tree> decls;
  collect_labels (body, path, decls, ctx.labels);

  crug_frame top;
  top.bind = body;
  ctx.frames.push_back (top);

  state_map env;
  for (tree a = DECL_ARGUMENTS (fndecl); a != NULL_TREE; a = DECL_CHAIN (a))
    if (TREE_CODE (a) == PARM_DECL && crug_is_owning_decl (a))
      {
	ctx.frames[0].decls.push_back (a);
	env[a] = crug_ostate (CRUG_OWNED, NULL_TREE);
      }

  /* A borrowed parameter is a live borrow of an object the caller owns: it
     has no local lender and no destruction (ADR 0018 D2 R7, ADR 0003 D1).  */
  for (tree a = DECL_ARGUMENTS (fndecl); a != NULL_TREE; a = DECL_CHAIN (a))
    {
      bool is_mut = false;
      if (TREE_CODE (a) != PARM_DECL || !crug_is_borrow_decl (a, &is_mut))
	continue;
      crug_borrow b;
      b.is_mut = is_mut;
      b.lender = NULL_TREE;
      b.depth = 0;
      ctx.frames[0].borrows.push_back (a);
      ctx.borrow_declared.insert (a);
      ctx.decl_depth[a] = 0;
      ctx.borrows[a] = b;
    }

  bool reach = walk_stmt (ctx, env, BIND_EXPR_BODY (body));
  if (reach)
    {
      location_t loc = node_location (body);
      std::vector<crug_drop> drops;
      collect_drops (ctx, env, 0, loc, drops);
      if (!drops.empty ())
	ctx.scope_drops[body] = drops;
      dump_drops (CRUG_EXIT_SCOPE_END, loc, drops);
    }

  check_gotos (ctx);
  ctx.frames.pop_back ();

  walk_tree_without_duplicates (&body, rewrite_cb, &ctx);
  /* Any intrinsic left in code that pass A never reached must still
     disappear before the middle end sees it.  */
  walk_tree_without_duplicates (&body, lower_intrinsic_cb, NULL);
}
