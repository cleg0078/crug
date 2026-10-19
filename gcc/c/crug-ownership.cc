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
#include "c-family/crug-ffi.h"
#include "crug-ownership.h"
#include "crug-safe.h"
#include "crug-tasks.h"
#include "crug-chan.h"
#include "crug-send-sync.h"

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
   without a destructor leaks, and the -Wcrug-unadopted-own warning says so.

   An owning payload enum (ADR 0018 D8) is destroyed conditionally, because
   only the active variant owns a payload.  VARIANT_DTORS maps the variant
   indices that this object may hold to the destructor that variant needs; an
   entry with a NULL destructor means the variant owns nothing (a unit
   variant or a non-owning payload) and needs no call.  An empty map is the
   "no destructor known" case, which leaks like a NULL DTOR.  */

struct crug_ostate
{
  crug_state state;
  tree dtor;
  std::map<int, tree> variant_dtors;

  crug_ostate () : state (CRUG_EMPTY), dtor (NULL_TREE) {}
  crug_ostate (crug_state s, tree d) : state (s), dtor (d) {}
};

typedef std::map<tree, crug_ostate> state_map;

/* One destruction that an exit edge performs.  For an owning payload enum,
   ENUM_TYPE is the payload enum type and VARIANT_DTORS holds the destructor
   of each variant that owns a payload; DTOR then stays NULL_TREE.  */

struct crug_drop
{
  tree decl;
  tree dtor;
  tree enum_type;
  std::map<int, tree> variant_dtors;
  /* A scope wait: the group handle to leave.  It is emitted before the
     frame's own destructions, so a task that borrowed those objects can
     still read them while the scope waits (ADR 0024 D3).  */
  tree scope_handle;

  crug_drop ()
    : decl (NULL_TREE), dtor (NULL_TREE), enum_type (NULL_TREE),
      scope_handle (NULL_TREE)
  {}
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
  /* The group handle when this frame is the body of a 'scope' statement:
     leaving the frame waits for the group first.  */
  tree scope_handle;

  crug_frame () : bind (NULL_TREE), scope_handle (NULL_TREE) {}
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
  /* Where the borrow was created and how deep the lender lives.  The origin
     is used by the crug-BORROW-019 note, LENDER_DEPTH by the escape rules.  */
  location_t origin;
  size_t lender_depth;
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
  /* The ownership state of a match scrutinee temporary when the match
     consumed it.  The temporary is not dropped itself; its destructors were
     either transferred to an arm's binding or belong to a variant that owns
     nothing (see crug_register_match_scrutinee).  */
  std::map<tree, crug_ostate> match_scrutinee_state;
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
  /* Last-use liveness of borrows (Phase 7).  LIVE_BEFORE and LIVE_AFTER hold,
     for every statement the analysis visits, the borrow bindings that are
     live on entry and on exit; ACTIVE_BORROWS is the set in force while the
     current statement is analysed.  LIVENESS_FAILED means the function
     contains a construct the dataflow does not model (a goto, or a loop that
     did not reach a fixpoint), and every registered borrow then counts as
     live, which is the conservative rule of GAPS section 10.3.  */
  std::map<tree, std::set<tree> > live_before;
  std::map<tree, std::set<tree> > live_after;
  std::set<tree> active_borrows;
  std::set<tree> borrow_ended;
  /* Every borrow binding the function declares, collected before the walk so
     that a loop body can be solved without a fixpoint.  */
  std::set<tree> all_borrows;
  bool liveness_failed = false;
  /* How many 'unsafe { }' blocks enclose the statement being analysed.
     Strict-mode checks stand down while this is non-zero (ADR 0021 D3).  */
  int unsafe_depth = 0;
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

/* Declarations recorded as raw (unchecked) pointers, keyed the same way.  A
   raw pointer is a plain C pointer with a promise that it is not to be
   dereferenced in checked code (RFC-0001 section 20; ADR 0021 D2).  */
static std::set<unsigned int> crug_raw_uids;

/* The compound statements of 'unsafe { }' blocks in the current translation
   unit.  The parser marks them while it parses; the analysis reads the mark
   and stands down inside them.  Nothing is attached to the tree, so the
   mark cannot reach the middle end or the object file.  */
static std::set<tree> crug_unsafe_blocks;

/* The compound statements of 'scope' blocks and the group handle each one
   created.  A scope statement is lowered to a block whose first statement
   assigns the group handle, so the block itself is what the analysis sees.  */
static std::map<tree, tree> crug_scope_blocks;

/* The function each scope block belongs to, so that analysing a nested
   function does not drop the enclosing function's registrations.  */
static std::map<tree, tree> crug_scope_owners;

/* One borrow handed to a task of a scope: the lender, where the capture is,
   and whether it is mutable.  */
struct crug_scoped_borrow
{
  tree lender;
  location_t loc;
  bool is_mut;
};

/* The borrows captured by scoped tasks, keyed by the group handle.  Children
   of one scope run at the same time, so the ordinary borrow rules apply
   between their captures, and every lender has to outlive the scope block
   (crug-CONC-016, checked when that block is analysed).  */
static std::map<tree, std::vector<crug_scoped_borrow> > crug_scoped_lenders;

/* The artificial function declarations used by the intrinsics.  They exist
   only inside the front end: every call is removed by this pass.  */

static tree crug_intr_fndecl[CRUG_INTR_BORROW_MUT + 1];

bool
crug_ownership_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_OWNERSHIP);
}

bool
crug_strict_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_STRICT);
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

/* Match statements: the parser registers the scrutinee temporary and every
   payload binding it creates (ADR 0018 D8 R4/R5).  */

struct crug_match_binding_reg
{
  tree tmp;
  int variant;
};

static std::map<tree, tree> *crug_match_scrutinee_map = NULL;
static std::map<tree, crug_match_binding_reg> *crug_match_binding_map = NULL;

void
crug_register_match_scrutinee (tree tmp)
{
  if (tmp == NULL_TREE)
    return;
  if (crug_match_scrutinee_map == NULL)
    crug_match_scrutinee_map = new std::map<tree, tree> ();
  (*crug_match_scrutinee_map)[tmp] = tmp;
}

void
crug_register_match_binding (tree binding, tree tmp, int variant)
{
  struct crug_match_binding_reg reg;

  if (binding == NULL_TREE || tmp == NULL_TREE)
    return;
  if (crug_match_binding_map == NULL)
    crug_match_binding_map = new std::map<tree, crug_match_binding_reg> ();
  reg.tmp = tmp;
  reg.variant = variant;
  (*crug_match_binding_map)[binding] = reg;
}

static bool
match_scrutinee_p (tree decl)
{
  return crug_match_scrutinee_map != NULL
    && crug_match_scrutinee_map->find (decl)
       != crug_match_scrutinee_map->end ();
}

static bool
match_binding_reg (tree decl, tree *tmp, int *variant)
{
  if (crug_match_binding_map == NULL)
    return false;
  std::map<tree, crug_match_binding_reg>::iterator it
    = crug_match_binding_map->find (decl);
  if (it == crug_match_binding_map->end ())
    return false;
  if (tmp != NULL)
    *tmp = it->second.tmp;
  if (variant != NULL)
    *variant = it->second.variant;
  return true;
}

/* True when TYPE is a payload enum that owns at least one payload.  Such a
   value is owning as a whole even though 'own' cannot be written on it: the
   qualifier belongs to the payload type-name, not to the enum type.  */
static bool
owns_payload_enum_type_p (tree type)
{
  if (type == NULL_TREE || type == error_mark_node)
    return false;
  type = TYPE_MAIN_VARIANT (type);
  return crug_payload_enum_type_p (type) && crug_payload_enum_owns_p (type);
}

/* True when DECL is an owned object: either an explicitly owning declarator
   ('own Foo *p') or a value of an owning payload enum type.  */
static bool
decl_owns (tree decl)
{
  if (decl == NULL_TREE || !DECL_P (decl))
    return false;
  if (crug_is_owning_decl (decl))
    return true;
  if (TREE_CODE (decl) != VAR_DECL && TREE_CODE (decl) != PARM_DECL)
    return false;
  return owns_payload_enum_type_p (TREE_TYPE (decl));
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

bool
crug_intrinsic_call_p (tree expr, enum crug_intrinsic want)
{
  enum crug_intrinsic kind;
  return is_intrinsic_call (expr, &kind) && kind == want;
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
crug_mark_raw_decl (tree decl, location_t loc)
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

  if (TREE_CODE (decl) == FUNCTION_DECL)
    {
      /* 'raw Foo *f (void)' declares a function that returns an unmanaged
	 pointer: the qualifier belongs to the return type, and the function
	 itself is not raw.  */
      crug_raw_uids.insert (DECL_UID (decl));
      return;
    }

  tree type = TREE_TYPE (decl);
  if (type == NULL_TREE || type == error_mark_node)
    return;

  if (TREE_CODE (type) == ARRAY_TYPE)
    type = TREE_TYPE (type);

  if (TREE_CODE (type) != POINTER_TYPE)
    {
      error_at (loc, "a %<raw%> declarator must have pointer type "
		"[crug-TYPE-001]");
      return;
    }

  if (TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    {
      error_at (loc, "a %<raw%> declarator cannot have pointer-to-function "
		"type [crug-TYPE-005]");
      return;
    }

  crug_raw_uids.insert (DECL_UID (decl));
}

bool
crug_is_raw_decl (tree decl)
{
  return (decl != NULL_TREE && DECL_P (decl) && DECL_NAME (decl) != NULL_TREE
	  && crug_raw_uids.count (DECL_UID (decl)) != 0);
}

void
crug_mark_unsafe_block (tree bind)
{
  if (bind != NULL_TREE && bind != error_mark_node)
    crug_unsafe_blocks.insert (bind);
}

bool
crug_is_unsafe_block (tree bind)
{
  return (bind != NULL_TREE && bind != error_mark_node
	  && crug_unsafe_blocks.count (bind) != 0);
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

  /* A borrow is a view of an object with a known lifetime, so it can only
     live in an automatic variable of the function that created it (ADR 0019,
     ADR 0005 A-1).  A static or file-scope borrow would outlive its referent,
     and a member or an indirect store has no lifetime this pass can see.  */
  /* DECL_CONTEXT is not set yet at this point of the declaration, so the
     scope is read from the parser state: a file-scope declaration is parsed
     with no current function, and a static local carries TREE_STATIC.  */
  if (TREE_CODE (decl) == VAR_DECL
      && (TREE_STATIC (decl) || current_function_decl == NULL_TREE))
    {
      error_at (loc, "a borrow must be an automatic variable; %qD would "
		"outlive its referent [crug-BORROW-018]", decl);
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

/* Alias facts recorded for the optimizer: DECL_UID -> 0 for 'mut', 1 for
   'own'.  The map is what the alias dump and its audit read, so a restrict
   qualifier the user wrote is never mistaken for a fact the compiler
   derived (ADR 0010 D3).  */

static std::map<unsigned int, int> crug_alias_fact_uids;

void
crug_apply_alias_fact (tree decl, bool is_mut)
{
  if (!crug_alias_facts_enabled ()
      || decl == NULL_TREE || decl == error_mark_node
      || TREE_CODE (decl) != PARM_DECL)
    return;

  tree type = TREE_TYPE (decl);
  if (type == NULL_TREE || type == error_mark_node
      || TREE_CODE (type) != POINTER_TYPE
      || TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE)
    return;

  tree qualified
    = build_qualified_type (type, TYPE_QUALS (type) | TYPE_QUAL_RESTRICT);
  if (qualified == type)
    return;

  TREE_TYPE (decl) = qualified;
  crug_alias_fact_uids[DECL_UID (decl)] = is_mut ? 0 : 1;
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

/* The ownership rules for one capture of a 'go' statement (ADR 0018 D4,
   MASTER_SPEC section 27.2).  A task starts later, so nothing about the
   caller's frame can be assumed: an owned value has to be moved into the task
   explicitly, a borrow cannot escape, and a destructor is not part of the
   callee's contract.  PARM is the callee parameter of that argument, or
   NULL_TREE for an indirect call whose type carries no declaration.

   Returns false after reporting a diagnostic; the caller then emits no code,
   so the program does not also get a cascade from the lowered frame.  */

/* The object a pointer expression designates, for the scoped-borrow rule.  */
static tree expr_base_decl (tree e);

/* Record the block of a 'scope' statement and its group handle.  The parser
   calls this once the block has been parsed, before the analysis runs.  */

void
crug_register_scope_block (tree bind, tree handle)
{
  if (bind == NULL_TREE || handle == NULL_TREE)
    return;
  crug_scope_blocks[bind] = handle;
  crug_scope_owners[bind] = current_function_decl;
}

/* Record that a scoped task borrows LENDER; the capture's location is where
   the diagnostic goes when the lender does not outlive the scope.  */

void
crug_register_scoped_lender (tree scope, tree lender, location_t loc,
			     bool is_mut)
{
  if (scope == NULL_TREE || lender == NULL_TREE)
    return;
  std::vector<crug_scoped_borrow> &captures = crug_scoped_lenders[scope];
  for (size_t i = 0; i < captures.size (); i++)
    if (captures[i].lender == lender && (is_mut || captures[i].is_mut))
      {
	if (is_mut && captures[i].is_mut)
	  error_at (loc, "cannot mutably borrow %qD for two tasks of one "
		    "scope; a mutable borrow must be exclusive "
		    "[crug-BORROW-003]", lender);
	else
	  error_at (loc, "cannot borrow %qD mutably and again in one scope; "
		    "a mutable borrow must be exclusive [crug-BORROW-002]",
		    lender);
	return;
      }
  crug_scoped_borrow b;
  b.lender = lender;
  b.loc = loc;
  b.is_mut = is_mut;
  captures.push_back (b);
}

bool
crug_check_go_capture (location_t loc, tree arg, tree parm, tree scope)
{
  if (arg == NULL_TREE || arg == error_mark_node)
    return true;

  /* A thread-affine value belongs to one thread and may not migrate; the
     check runs before anything else, because no ownership rule can make the
     move legal (ADR 0026 D5).  */
  {
    const char *affinity = NULL;

    if (crug_expr_thread_affine_p (arg, &affinity))
      {
	error_at (loc, "a thread-affine value cannot cross to another task; "
		  "it belongs to the %qs thread [crug-CONC-025]",
		  affinity == NULL ? "unknown" : affinity);
	return false;
      }
  }

  enum crug_intrinsic kind;
  if (TREE_CODE (arg) == CALL_EXPR && is_intrinsic_call (arg, &kind))
    {
      if (kind == CRUG_INTR_BORROW || kind == CRUG_INTR_BORROW_MUT)
	{
	  bool is_mut = kind == CRUG_INTR_BORROW_MUT;
	  tree lender = NULL_TREE;
	  tree lender_type = NULL_TREE;
	  tree blocker = NULL_TREE;

	  if (scope == NULL_TREE)
	    {
	      error_at (loc, "a borrow cannot be captured by a concurrent task; "
			"capture an owned value with %<move (...)%> [crug-CONC-012]");
	      return false;
	    }
	  /* A scoped task may borrow: the scope waits for every child before
	     the block ends, so the lent object still exists while the task can
	     read it.  The lender has to outlive the scope block itself, which
	     is checked when that block is analysed (crug-CONC-016).  */
	  if (call_expr_nargs (arg) >= 1)
	    lender = expr_base_decl (CALL_EXPR_ARG (arg, 0));
	  crug_register_scoped_lender (scope, lender, loc, is_mut);

	  /* The child and the scope's owner then share the object, so a shared
	     borrow needs Sync and a mutable borrow needs Send (ADR 0026 D3).  */
	  lender_type = TREE_TYPE (arg);
	  if (TREE_CODE (lender_type) == POINTER_TYPE
	      && TREE_TYPE (lender_type) != NULL_TREE)
	    lender_type = TREE_TYPE (lender_type);
	  if (is_mut)
	    {
	      if (!crug_type_send_p (lender_type, &blocker))
		{
		  error_at (loc, "a value of type %qT cannot cross to another "
			    "task; the type is not %<Send%> [crug-CONC-024]",
			    lender_type);
		  if (blocker != NULL_TREE && blocker != lender_type)
		    inform (loc, "%qT contains %qT, which is not %<Send%>",
			    lender_type, blocker);
		  return false;
		}
	    }
	  else if (!crug_type_sync_p (lender_type, &blocker))
	    {
	      error_at (loc, "a shared borrow of %qT cannot cross to another "
			"task; the type is not %<Sync%> [crug-CONC-026]",
			lender_type);
	      if (blocker != NULL_TREE && blocker != lender_type)
		inform (loc, "%qT contains %qT, which is not %<Sync%>",
			lender_type, blocker);
	      return false;
	    }
	  return true;
	}
      if (kind == CRUG_INTR_MOVE)
	{
	  tree moved = TREE_TYPE (arg);
	  tree probe;
	  tree blocker = NULL_TREE;

	  if (parm != NULL_TREE && !crug_is_owning_decl (parm))
	    {
	      error_at (loc, "the task does not take ownership of the captured "
			"value: declare the parameter %<own%> [crug-CONC-011]");
	      return false;
	    }
	  /* The task owns the value, so its type must be Send.  */
	  probe = (TREE_CODE (moved) == POINTER_TYPE
		   && TREE_TYPE (moved) != NULL_TREE)
		  ? TREE_TYPE (moved) : moved;
	  if (!crug_type_send_p (probe, &blocker))
	    {
	      error_at (loc, "a value of type %qT cannot cross to another task; "
			"the type is not %<Send%> [crug-CONC-024]", probe);
	      if (blocker != NULL_TREE && blocker != probe)
		inform (loc, "%qT contains %qT, which is not %<Send%>", probe,
			blocker);
	      return false;
	    }
	  return true;
	}
      if (kind == CRUG_INTR_ADOPT)
	{
	  error_at (loc, "a destructor cannot cross a task boundary; the task "
		    "destroys an owned parameter by itself [crug-CONC-014]");
	  return false;
	}
      return true;
    }

  tree decl = arg;
  STRIP_NOPS (decl);

  if (decl != NULL_TREE && DECL_P (decl))
    {
      if (crug_is_owning_decl (decl))
	{
	  error_at (loc, "owned value passed to concurrent task without "
		    "ownership transfer [crug-CONC-011]");
	  inform (loc, "use %<move (%E)%> to transfer it to the task", decl);
	  return false;
	}

      if (crug_is_borrow_decl (decl, NULL))
	{
	  error_at (loc, "a borrow cannot be captured by a concurrent task; "
		    "capture an owned value with %<move (...)%> [crug-CONC-012]");
	  return false;
	}
    }

  /* Whatever is left is copied into the task's frame, so its type has to be
     Send (ADR 0026 D2).  A plain expression is checked here as well, not
     only a declaration.  */
  {
    tree type = TREE_TYPE (arg);
    tree blocker = NULL_TREE;

    if (type != NULL_TREE
	&& TREE_CODE (type) != ERROR_MARK
	&& !crug_type_send_p (type, &blocker))
      {
	error_at (loc, "a value of type %qT cannot cross to another task; "
		  "the type is not %<Send%> [crug-CONC-024]", type);
	if (blocker != NULL_TREE && blocker != type)
	  inform (loc, "%qT contains %qT, which is not %<Send%>", type,
		  blocker);
	return false;
      }
  }
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
  { "borrow", ".crug-borrow", "crug borrow dump v1", NULL, false },
  { "alias", ".crug-alias", "crug alias dump v1", NULL, false }
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

/* The alias facts this function proved: one line per 'mut' or 'own' pointer
   parameter that was given to the middle end as 'restrict' (ADR 0010).  The
   dump is the audit trail: a restrict qualifier that is not in here is one
   the user wrote, and a line here must correspond to a mut/own parameter.  */

static void
dump_alias_facts (tree fndecl)
{
  if (!dump_kind_enabled ("alias"))
    return;

  FILE *file = get_dump_file ("alias");
  if (file == NULL)
    return;

  const char *fname = (fndecl != NULL_TREE && DECL_NAME (fndecl) != NULL_TREE)
    ? IDENTIFIER_POINTER (DECL_NAME (fndecl)) : "<anonymous>";

  for (tree a = DECL_ARGUMENTS (fndecl); a != NULL_TREE; a = DECL_CHAIN (a))
    {
      if (TREE_CODE (a) != PARM_DECL || DECL_NAME (a) == NULL_TREE)
	continue;
      std::map<unsigned int, int>::const_iterator it
	= crug_alias_fact_uids.find (DECL_UID (a));
      if (it == crug_alias_fact_uids.end ())
	continue;
      tree type = TREE_TYPE (a);
      if (type == NULL_TREE || TREE_CODE (type) != POINTER_TYPE
	  || (TYPE_QUALS (type) & TYPE_QUAL_RESTRICT) == 0)
	continue;
      fprintf (file, "alias %s: parameter %s restrict (%s)\n",
	       fname, IDENTIFIER_POINTER (DECL_NAME (a)),
	       it->second == 0 ? "mut" : "own");
    }
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
    {
      const crug_drop &dr = drops[i];
      if (dr.scope_handle != NULL_TREE)
	{
	  fprintf (file, "drop %s:%d: %s scope wait\n",
		   where.file != NULL ? lbasename (where.file) : "<unknown>",
		   where.line, exit_kind_name (kind));
	  continue;
	}
      if (dr.enum_type != NULL_TREE)
	{
	  /* An owning payload enum is destroyed per variant, so name the
	     variants whose destructor this exit can call.  A variant that
	     owns nothing has no entry here, which is the drop=0 evidence.  */
	  for (std::map<int, tree>::const_iterator vi
		 = dr.variant_dtors.begin ();
	       vi != dr.variant_dtors.end (); ++vi)
	    {
	      if (vi->second == NULL_TREE)
		continue;
	      fprintf (file, "drop %s:%d: %s %s [%s] via %s\n",
		       where.file != NULL ? lbasename (where.file) : "<unknown>",
		       where.line, exit_kind_name (kind),
		       IDENTIFIER_POINTER (DECL_NAME (dr.decl)),
		       crug_payload_enum_variant_name (dr.enum_type, vi->first),
		       DECL_NAME (vi->second) != NULL_TREE
		       ? IDENTIFIER_POINTER (DECL_NAME (vi->second)) : "none");
	    }
	  continue;
	}
      fprintf (file, "drop %s:%d: %s %s via %s\n",
	       where.file != NULL ? lbasename (where.file) : "<unknown>",
	       where.line, exit_kind_name (kind),
	       IDENTIFIER_POINTER (DECL_NAME (dr.decl)),
	       dr.dtor != NULL_TREE && DECL_NAME (dr.dtor) != NULL_TREE
	       ? IDENTIFIER_POINTER (DECL_NAME (dr.dtor)) : "none");
    }
}

/* A borrow begins at its declaration and ends with the block that declares
   it.  The root lender is printed, so a reborrow is shown against the object
   that is actually borrowed.  */

static void
dump_borrow (bool begin, location_t loc, tree binding, bool is_mut, tree lender,
	     const char *reason = NULL)
{
  if (!dump_kind_enabled ("borrow"))
    return;

  FILE *file = get_dump_file ("borrow");
  if (file == NULL || DECL_NAME (binding) == NULL_TREE)
    return;

  expanded_location where = expand_location (loc);
  fprintf (file, "%s %s:%d: %s %s %s",
	   begin ? "borrow" : "end",
	   where.file != NULL ? lbasename (where.file) : "<unknown>",
	   where.line,
	   IDENTIFIER_POINTER (DECL_NAME (binding)),
	   is_mut ? "excludes" : "shares",
	   lender != NULL_TREE && DECL_NAME (lender) != NULL_TREE
	   ? IDENTIFIER_POINTER (DECL_NAME (lender)) : "<parameter>");
  if (reason != NULL)
    fprintf (file, " (%s)", reason);
  fprintf (file, "\n");
}

/* Last-use liveness of borrows (Phase 7).

   Phase 6 ended a borrow at the end of the block that declares it.  That is
   sound but rejects correct programs: a shared borrow whose last use is
   before a mutable borrow does not conflict with it, and an object can be
   moved or destroyed once no borrow of it is read again.  This pass computes,
   for every statement of the structured GENERIC body, the borrow bindings
   that are live on entry and on exit, using the standard backward equations

     in = use (S) | (out - def (S))

   and taking the union at a join, which is the sound direction: a borrow is
   live if any path still uses it.  No CFG is built and no IR is changed; the
   recursion follows the same shape as the checking walk, so the two cannot
   disagree about what a statement is.  */

typedef std::set<tree> live_set;

/* Collect the borrow bindings read (DEFS false) or written (DEFS true) by T.
   A DECL_EXPR defines its declared binding and reads its initializer; for any
   other statement the two sets are derived from its operands.  */

static tree
collect_borrow_use_cb (tree *tp, int *, void *data)
{
  tree t = *tp;
  if (t != NULL_TREE && t != error_mark_node
      && (TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == PARM_DECL)
      && crug_is_borrow_decl (t, NULL))
    static_cast<live_set *> (data)->insert (t);
  return NULL_TREE;
}

static void
collect_borrow_refs (tree t, bool defs, live_set &out)
{
  if (t == NULL_TREE || t == error_mark_node)
    return;

  switch (TREE_CODE (t))
    {
    case DECL_EXPR:
      {
	tree d = DECL_EXPR_DECL (t);
	if (d != NULL_TREE && TREE_CODE (d) == VAR_DECL)
	  {
	    if (defs && crug_is_borrow_decl (d, NULL))
	      out.insert (d);
	    /* The initializer is read even when the declaration is not a
	       borrow: it can use another borrow.  */
	    collect_borrow_refs (DECL_INITIAL (d), false, out);
	  }
	return;
      }

    case MODIFY_EXPR:
      {
	tree lhs = TREE_OPERAND (t, 0);
	bool lhs_is_def = defs && lhs != NULL_TREE && TREE_CODE (lhs) == VAR_DECL
	  && crug_is_borrow_decl (lhs, NULL);
	if (lhs_is_def)
	  out.insert (lhs);
	else
	  collect_borrow_refs (lhs, false, out);
	collect_borrow_refs (TREE_OPERAND (t, 1), false, out);
	return;
      }

    case BIND_EXPR:
      collect_borrow_refs (BIND_EXPR_BODY (t), defs, out);
      return;

    case STATEMENT_LIST:
      for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	collect_borrow_refs (tsi_stmt (i), defs, out);
      return;

    case LABEL_EXPR:
    case CASE_LABEL_EXPR:
      return;

    default:
      break;
    }

  if (TREE_CODE (t) == VAR_DECL || TREE_CODE (t) == PARM_DECL)
    {
      if (!defs && crug_is_borrow_decl (t, NULL))
	out.insert (t);
      return;
    }

  /* Every other node: let walk_tree find the bindings, whatever the shape of
     the expression.  A hand-written operand walk missed `(void) r`, which is a
     NOP_EXPR and therefore neither tcc_expression, tcc_statement nor
     tcc_reference, and with it every borrow used only through a conversion.  */
  walk_tree_without_duplicates (&t, collect_borrow_use_cb, &out);
}

static live_set
solve_borrow_liveness (crug_fn_ctx &ctx, tree t, const live_set &out);

/* The live-in set of a loop, as the fixpoint of "the body's in set is what
   the next iteration needs".  A loop that does not converge within eight
   passes abandons liveness for the whole function rather than guess.  */

/* A loop needs a fixpoint, and a wrong fixpoint here would silently drop a
   borrow that is still in use.  Rather than approximate the loop-back edge,
   every borrow declared anywhere in the function is treated as live inside a
   loop: the body is solved once with all of them in its out set.  That is the
   conservative rule of GAPS section 10.3 and it costs nothing outside the
   loop, because the loop's own live-out set is not widened.  */

static live_set
liveness_loop (crug_fn_ctx &ctx, tree body, tree cond, tree incr,
	       const live_set &out)
{
  live_set body_out = out;
  for (live_set::const_iterator i = ctx.all_borrows.begin ();
       i != ctx.all_borrows.end (); ++i)
    body_out.insert (*i);

  live_set body_in = body != NULL_TREE
    ? solve_borrow_liveness (ctx, body, body_out) : body_out;

  if (incr != NULL_TREE)
    body_in = solve_borrow_liveness (ctx, incr, body_in);

  live_set after = body_in;
  for (live_set::const_iterator i = out.begin (); i != out.end (); ++i)
    after.insert (*i);

  return cond != NULL_TREE
    ? solve_borrow_liveness (ctx, cond, after) : after;
}

/* The live-in set of statement T given LIVE OUT.  Every visited statement is
   recorded, because the checking walk asks for its set by node.  */

static live_set
solve_borrow_liveness (crug_fn_ctx &ctx, tree t, const live_set &out)
{
  if (t == NULL_TREE || t == error_mark_node)
    return out;

  live_set in;

  switch (TREE_CODE (t))
    {
    case BIND_EXPR:
      in = solve_borrow_liveness (ctx, BIND_EXPR_BODY (t), out);
      break;

    case STATEMENT_LIST:
      {
	live_set curr = out;
	/* Backward dataflow: walk the list from its last statement to its first,
	   carrying the live set that the statements after the current one need.  */
	for (tree_stmt_iterator i = tsi_last (t); !tsi_end_p (i); tsi_prev (&i))
	  curr = solve_borrow_liveness (ctx, tsi_stmt (i), curr);
	in = curr;
	break;
      }

    case COND_EXPR:
      if (!VOID_TYPE_P (TREE_TYPE (t)))
	{
	  live_set uses;
	  collect_borrow_refs (t, false, uses);
	  in = out;
	  for (live_set::const_iterator i = uses.begin (); i != uses.end (); ++i)
	    in.insert (*i);
	  break;
	}
      {
	live_set uses;
	collect_borrow_refs (TREE_OPERAND (t, 0), false, uses);
	live_set a = solve_borrow_liveness (ctx, TREE_OPERAND (t, 1), out);
	live_set b = solve_borrow_liveness (ctx, TREE_OPERAND (t, 2), out);
	in = uses;
	for (live_set::const_iterator i = a.begin (); i != a.end (); ++i)
	  in.insert (*i);
	for (live_set::const_iterator i = b.begin (); i != b.end (); ++i)
	  in.insert (*i);
	break;
      }

    case FOR_STMT:
      in = liveness_loop (ctx, FOR_BODY (t), FOR_COND (t), FOR_EXPR (t), out);
      if (FOR_INIT_STMT (t) != NULL_TREE)
	in = solve_borrow_liveness (ctx, FOR_INIT_STMT (t), in);
      break;

    case WHILE_STMT:
      in = liveness_loop (ctx, WHILE_BODY (t), WHILE_COND (t), NULL_TREE, out);
      break;

    case DO_STMT:
      in = liveness_loop (ctx, DO_BODY (t), DO_COND (t), NULL_TREE, out);
      break;

    case SWITCH_STMT:
      {
	live_set uses;
	collect_borrow_refs (SWITCH_COND (t), false, uses);
	live_set body = solve_borrow_liveness (ctx, SWITCH_BODY (t), out);
	in = uses;
	for (live_set::const_iterator i = body.begin (); i != body.end (); ++i)
	  in.insert (*i);
	break;
      }

    case LABEL_EXPR:
    case CASE_LABEL_EXPR:
      /* Labels are only reachable through a goto, and a function with a goto
	 does not get liveness at all.  */
      in = out;
      break;

    case RETURN_EXPR:
    case BREAK_STMT:
    case CONTINUE_STMT:
    case GOTO_EXPR:
      {
	live_set uses;
	collect_borrow_refs (t, false, uses);
	in = uses;
	break;
      }

    default:
      {
	live_set uses;
	live_set defs;
	collect_borrow_refs (t, false, uses);
	collect_borrow_refs (t, true, defs);
	in = uses;
	for (live_set::const_iterator i = out.begin (); i != out.end (); ++i)
	  if (defs.find (*i) == defs.end ())
	    in.insert (*i);
	break;
      }
    }

  ctx.live_before[t] = in;
  ctx.live_after[t] = out;
  return in;
}

/* A goto makes the structured walk an incomplete model of control flow, so
   liveness is abandoned rather than approximated.  */

static tree
find_goto_cb (tree *tp, int *, void *)
{
  if (*tp != NULL_TREE && TREE_CODE (*tp) == GOTO_EXPR)
    return *tp;
  return NULL_TREE;
}

static void
borrow_liveness (crug_fn_ctx &ctx)
{
  tree body = ctx.body;
  if (body == NULL_TREE)
    return;

  /* The body walk registers the borrows it meets, so the declarations are
     collected syntactically here; a borrowed parameter is already in
     CTX.BORROWS and needs no declaration.  */
  collect_borrow_refs (body, true, ctx.all_borrows);
  if (ctx.all_borrows.empty () && ctx.borrows.empty ())
    return;

  if (walk_tree_without_duplicates (&body, find_goto_cb, NULL) != NULL_TREE)
    {
      ctx.liveness_failed = true;
      return;
    }

  live_set out;
  solve_borrow_liveness (ctx, body, out);
}

/* The analysis proper.  */

static bool walk_stmt (crug_fn_ctx &ctx, state_map &env, tree t,
		       location_t locus = UNKNOWN_LOCATION);
static void walk_expr (crug_fn_ctx &ctx, state_map &env, tree t,
		       location_t locus = UNKNOWN_LOCATION);
/* The FUNCTION_DECL a call names, or NULL_TREE (defined below).  */
static tree crug_call_fndecl (tree call);
/* The declaration E names after removing conversions, or NULL_TREE.  */
static tree nop_decl (tree e);

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

/* End the borrows whose last use was CUR, at the point where NEXT begins.
   The dump says why a borrow ended, so a reader can tell a last-use end from
   a scope end.  */

static void
end_borrows_at (crug_fn_ctx &ctx, tree cur, tree next)
{
  if (ctx.liveness_failed || !dump_kind_enabled ("borrow"))
    return;

  std::map<tree, live_set>::const_iterator nb = ctx.live_before.find (next);
  if (nb == ctx.live_before.end ())
    return;

  std::map<tree, live_set>::const_iterator cb = ctx.live_before.find (cur);

  for (std::map<tree, crug_borrow>::const_iterator i = ctx.borrows.begin ();
       i != ctx.borrows.end (); ++i)
    {
      tree b = i->first;
      if (TREE_CODE (b) == PARM_DECL
	  || ctx.borrow_ended.find (b) != ctx.borrow_ended.end ())
	continue;

      bool live_here = cb != ctx.live_before.end ()
	&& cb->second.find (b) != cb->second.end ();
      if (!live_here && TREE_CODE (cur) == DECL_EXPR
	  && DECL_EXPR_DECL (cur) == b)
	live_here = true;

      if (live_here && nb->second.find (b) == nb->second.end ())
	{
	  dump_borrow (false, node_location (next), b, i->second.is_mut,
		       i->second.lender, "last-use");
	  ctx.borrow_ended.insert (b);
	}
    }
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

/* Copy the ownership that SRC confers onto DECL.  */
static void
set_owned_from (state_map &env, tree decl, const crug_ostate &src)
{
  env[decl] = crug_ostate (CRUG_OWNED, src.dtor);
  env[decl].variant_dtors = src.variant_dtors;
}

/* True when destroying DECL would call something.  For an owning payload
   enum that depends on the variant, and the map must be non-empty: an empty
   map means no destructor is known at all, which is the leak case.  */
static bool
state_needs_drop (const crug_ostate &st, tree decl)
{
  if (owns_payload_enum_type_p (TREE_TYPE (decl)))
    {
      for (std::map<int, tree>::const_iterator i = st.variant_dtors.begin ();
	   i != st.variant_dtors.end (); ++i)
	if (i->second != NULL_TREE)
	  return true;
      return false;
    }
  return st.dtor != NULL_TREE;
}

/* True when no destructor is known for the owned object DECL, which is what
   -Wcrug-unadopted-own reports.  */
static bool
state_dtor_unknown (const crug_ostate &st, tree decl)
{
  if (owns_payload_enum_type_p (TREE_TYPE (decl)))
    return st.variant_dtors.empty ();
  return st.dtor == NULL_TREE;
}

static crug_drop
make_drop (tree decl, const crug_ostate &st)
{
  crug_drop dr;

  dr.decl = decl;
  if (owns_payload_enum_type_p (TREE_TYPE (decl)))
    {
      dr.enum_type = TYPE_MAIN_VARIANT (TREE_TYPE (decl));
      dr.variant_dtors = st.variant_dtors;
    }
  else
    dr.dtor = st.dtor;
  return dr;
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
  f.scope_handle = NULL_TREE;
  std::map<tree, tree>::const_iterator s = crug_scope_blocks.find (bind);
  if (s != crug_scope_blocks.end ())
    f.scope_handle = s->second;
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
	  /* A borrow already ended at its last use is not reported again.  */
	  if (ctx.borrow_ended.find (f.borrows[i]) == ctx.borrow_ended.end ())
	    dump_borrow (false, node_location (f.bind), f.borrows[i],
			 b->second.is_mut, b->second.lender, "scope-end");
	  ctx.borrows.erase (b);
	}
      ctx.decl_depth.erase (f.borrows[i]);
    }
  /* The block is done; a later block never reuses this handle.  */
  if (f.scope_handle != NULL_TREE)
    {
      crug_scope_blocks.erase (f.bind);
      crug_scope_owners.erase (f.bind);
    }
  ctx.frames.pop_back ();
}

/* Every lender a scoped task borrowed has to outlive the scope block.  A
   lender declared in a block that ends before the scope dies while a child
   may still read it, so that capture is rejected (ADR 0024 D4).  DEPTH is
   the frame index of the scope block itself.  */

static void
check_scoped_lenders (crug_fn_ctx &ctx, tree bind, size_t depth)
{
  std::map<tree, tree>::const_iterator s = crug_scope_blocks.find (bind);
  if (s == crug_scope_blocks.end ())
    return;
  std::map<tree, std::vector<crug_scoped_borrow> >::iterator it
    = crug_scoped_lenders.find (s->second);
  if (it == crug_scoped_lenders.end ())
    return;
  for (size_t i = 0; i < it->second.size (); i++)
    {
      tree lender = it->second[i].lender;
      location_t loc = it->second[i].loc;
      std::map<tree, size_t>::const_iterator d = ctx.decl_depth.find (lender);
      /* A global, a static or a parameter lives past the scope.  */
      if (d == ctx.decl_depth.end ())
	continue;
      if (d->second > depth && ctx.warned.insert (lender).second)
	error_at (loc, "a scoped task borrows %qD from a block that ends before "
		  "the scope; declare it in the scope body [crug-CONC-016]",
		  lender);
    }
  crug_scoped_lenders.erase (it);
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
register_borrow (crug_fn_ctx &ctx, tree binding, bool is_mut, tree lender,
		 location_t origin)
{
  crug_borrow b;
  b.is_mut = is_mut;
  b.lender = lender;
  b.depth = ctx.frames.size () - 1;
  b.origin = origin;
  /* A borrowed parameter has no local lender, so it lives as long as the
     function; every other lender has a frame depth of its own.  */
  std::map<tree, size_t>::const_iterator ld = ctx.decl_depth.find (lender);
  b.lender_depth = ld != ctx.decl_depth.end () ? ld->second : 0;
  ctx.frames.back ().borrows.push_back (binding);
  ctx.borrow_declared.insert (binding);
  ctx.decl_depth[binding] = b.depth;
  ctx.borrows[binding] = b;
  ctx.borrow_ended.erase (binding);
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

      /* Phase 7: a borrow stops excluding its lender at its last use.  A
	 borrowed parameter always counts as live, since its lifetime is the
	 caller's, and a function the liveness pass could not model falls back
	 to the lexical rule (every registered borrow is live).  */
      if (!ctx.liveness_failed
	  && TREE_CODE (i->first) != PARM_DECL
	  && ctx.active_borrows.find (i->first) == ctx.active_borrows.end ())
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
      crug_ostate merged (result, choose_dtor (ctx, d, sa.dtor, sb.dtor, loc));
      /* Merge the per-variant destructors of an owning payload enum: a
	 variant that only one path can hold keeps its destructor, and two
	 different destructors for the same variant conflict (crug-OWN-010).  */
      merged.variant_dtors = sa.variant_dtors;
      for (std::map<int, tree>::const_iterator i = sb.variant_dtors.begin ();
	   i != sb.variant_dtors.end (); ++i)
	{
	  std::map<int, tree>::iterator j = merged.variant_dtors.find (i->first);
	  if (j == merged.variant_dtors.end ())
	    merged.variant_dtors[i->first] = i->second;
	  else
	    j->second = choose_dtor (ctx, d, j->second, i->second, loc);
	}
      dst[d] = merged;
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
      /* Waiting for the group comes before destroying the objects of the
	 block: a child may still use an object it borrowed (ADR 0024 D3).  */
      if (f.scope_handle != NULL_TREE)
	{
	  crug_drop wait;
	  wait.decl = f.scope_handle;
	  wait.scope_handle = f.scope_handle;
	  out.push_back (wait);
	}
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
	      if (state_needs_drop (st, d))
		out.push_back (make_drop (d, st));
	      else if (state_dtor_unknown (st, d))
		warn_unadopted (ctx, d, loc);
	      break;
	    case CRUG_MAYBE_MOVED:
	    case CRUG_MAYBE_EMPTY:
	      if (state_needs_drop (st, d))
		ambiguous_state (ctx, d, st.state, loc);
	      else if (state_dtor_unknown (st, d))
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

/* Mark an object as moved out of and return the ownership it passed on.  */

static crug_ostate
process_move (crug_fn_ctx &ctx, state_map &env, tree arg, location_t loc)
{
  if (arg == NULL_TREE || arg == error_mark_node)
    return crug_ostate ();

  tree r = arg;
  STRIP_NOPS (r);

  if (TREE_CODE (r) == COMPONENT_REF || TREE_CODE (r) == ARRAY_REF)
    {
      error_at (loc, "move of aggregate field %qE is not supported; move the "
		"whole object [crug-UAM-006]", r);
      return crug_ostate ();
    }

  if (TREE_CODE (r) != VAR_DECL && TREE_CODE (r) != PARM_DECL)
    {
      /* A prvalue: there is no object to move out of.  */
      walk_expr (ctx, env, arg, loc);
      return crug_ostate ();
    }

  if (!decl_owns (r))
    {
      error_at (loc, "move() operand is not an owned object [crug-OWN-001]");
      return crug_ostate ();
    }

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, r, &shared, &mutborrow))
    error_at (loc, "cannot move %qD while it is borrowed [crug-BORROW-004]", r);

  crug_ostate moved;
  state_map::const_iterator it = env.find (r);
  if (it != env.end ())
    moved = it->second;

  crug_state st = state_of (env, r);

  if (st == CRUG_MOVED)
    error_at (loc, "move of already-moved value %qD [crug-UAM-002]", r);
  else if (st == CRUG_MAYBE_MOVED)
    error_at (loc, "move of possibly-moved value %qD after a branch join "
	      "[crug-UAM-004]", r);

  env[r] = crug_ostate (CRUG_MOVED, NULL_TREE);
  if (st == CRUG_MOVED || st == CRUG_MAYBE_MOVED)
    return crug_ostate ();
  return moved;
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

  /* adopt takes ownership of its operand.  When that operand is an owned
     lvalue, adopt consumes it exactly as move does; the destructor attached
     to the new binding is the one given here (ADR 0004 D10).  */
  tree operand = raw;
  STRIP_NOPS (operand);
  if ((TREE_CODE (operand) == VAR_DECL || TREE_CODE (operand) == PARM_DECL)
      && crug_is_owning_decl (operand))
    {
      crug_state st = state_of (env, operand);
      if (st == CRUG_MOVED)
	error_at (loc, "use of moved value %qD [crug-UAM-001]", operand);
      else if (st == CRUG_MAYBE_MOVED)
	error_at (loc, "use of possibly-moved value %qD after a branch join "
		  "[crug-UAM-004]", operand);
      else
	{
	  bool shared = false;
	  bool mutborrow = false;
	  if (has_live_borrow (ctx, operand, &shared, &mutborrow))
	    error_at (loc, "cannot move %qD while it is borrowed "
		      "[crug-BORROW-004]", operand);
	}
      env[operand] = crug_ostate (CRUG_MOVED, NULL_TREE);
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
      || !decl_owns (r))
    {
      error_at (loc, "drop() operand is not an owned object [crug-DROP-001]");
      return;
    }

  bool shared = false;
  bool mutborrow = false;
  if (has_live_borrow (ctx, r, &shared, &mutborrow))
    error_at (loc, "cannot drop %qD while it is borrowed [crug-BORROW-005]", r);

  crug_state st = state_of (env, r);

  switch (st)
    {
    case CRUG_EMPTY:
      break;
    case CRUG_MOVED:
      error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
      break;
    case CRUG_MAYBE_MOVED:
    case CRUG_MAYBE_EMPTY:
      if (state_needs_drop (env[r], r))
	ambiguous_state (ctx, r, st, loc);
      else if (state_dtor_unknown (env[r], r))
	warn_unadopted (ctx, r, loc);
      break;
    case CRUG_OWNED:
      if (state_needs_drop (env[r], r))
	ctx.explicit_drops[call].push_back (make_drop (r, env[r]));
      else if (!owns_payload_enum_type_p (TREE_TYPE (r)))
	warn_unadopted (ctx, r, loc);
      break;
    }

  env[r] = crug_ostate (CRUG_MOVED, NULL_TREE);
}

/* The destructor that the payload of an owned payload-enum variant brings
   with it.  The payload is either moved in (the destructor travels with the
   value), adopted with an explicit destructor, a fresh allocation, or null.
   Any other expression would leave an owned payload behind without a
   destructor, which is exactly the leak the ownership layer exists to
   prevent, so it is rejected (ADR 0018 D8 R4, ADR 0004 D8).  */

static tree
payload_transfer_dtor (crug_fn_ctx &ctx, state_map &env, tree payload,
		       location_t loc, tree enum_type, int variant)
{
  if (payload == NULL_TREE || payload == error_mark_node)
    return NULL_TREE;

  tree r = payload;
  STRIP_NOPS (r);

  enum crug_intrinsic kind;
  if (TREE_CODE (r) == CALL_EXPR && is_intrinsic_call (r, &kind))
    {
      if (kind == CRUG_INTR_MOVE)
	return process_move (ctx, env, CALL_EXPR_ARG (r, 0), loc).dtor;
      if (kind == CRUG_INTR_ADOPT)
	return check_adopt (ctx, env, r, loc);
    }

  if (is_null_pointer (r))
    {
      walk_expr (ctx, env, payload, loc);
      return NULL_TREE;
    }

  if (is_allocation_call (r))
    {
      walk_expr (ctx, env, payload, loc);
      return known_free ();
    }

  /* A call whose .crugffi contract says it returns an owned value hands the
     variant that value with the destructor the sidecar names (ADR 0007 D3).  */
  if (TREE_CODE (r) == CALL_EXPR)
    {
      tree ffi_dtor;
      if (crug_ffi_returns_owned (crug_call_fndecl (r), &ffi_dtor))
	{
	  walk_expr (ctx, env, payload, loc);
	  return ffi_dtor;
	}
    }

  walk_expr (ctx, env, payload, loc);
  error_at (loc, "the payload of owned variant %qs must be moved in, adopted, "
	    "or a fresh allocation; write move (...), adopt (...) or an "
	    "allocation call [crug-ADT-019]",
	    crug_payload_enum_variant_name (enum_type, variant));
  return NULL_TREE;
}

/* Assign the ownership state that EXPR confers on DECL when DECL holds an
   owning payload enum (ADR 0018 D8).  The state carries one destructor per
   variant the object may hold: a constructor supplies exactly one entry, and
   a move transfers the map of the source.  */

static void
assign_owning_enum (crug_fn_ctx &ctx, state_map &env, tree decl, tree expr,
		    location_t loc)
{
  tree type = TYPE_MAIN_VARIANT (TREE_TYPE (decl));
  bool is_scrutinee = match_scrutinee_p (decl);

  if (expr == NULL_TREE || expr == error_mark_node)
    {
      set_state (env, decl, CRUG_EMPTY);
      if (is_scrutinee)
	ctx.match_scrutinee_state[decl] = crug_ostate ();
      return;
    }

  tree r = expr;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
      && decl_owns (r))
    {
      crug_state s = state_of (env, r);
      if (s == CRUG_MOVED)
	error_at (loc, "use of moved value %qD [crug-UAM-001]", r);
      else if (s == CRUG_MAYBE_MOVED)
	error_at (loc, "use of possibly-moved value %qD after a branch join "
		  "[crug-UAM-004]", r);
      else if (is_scrutinee)
	error_at (loc, "an owned scrutinee must be moved into match: write "
		  "match (move (%qD)) [crug-ADT-015]", r);
      else
	error_at (loc, "implicit copy of owned value %qD; owned values are "
		  "not copyable [crug-UAM-003]", r);
      set_state (env, decl, CRUG_EMPTY);
      if (is_scrutinee)
	ctx.match_scrutinee_state[decl] = crug_ostate ();
      return;
    }

  enum crug_intrinsic kind;
  if (TREE_CODE (r) == CALL_EXPR && is_intrinsic_call (r, &kind)
      && kind == CRUG_INTR_MOVE)
    {
      crug_ostate moved = process_move (ctx, env, CALL_EXPR_ARG (r, 0), loc);
      set_owned_from (env, decl, moved);
      if (is_scrutinee)
	{
	  ctx.match_scrutinee_state[decl] = moved;
	  /* The temporary consumed the value: its payloads were either moved
	     into an arm binding or belong to a variant that owns nothing.  */
	  env[decl] = crug_ostate (CRUG_MOVED, NULL_TREE);
	}
      return;
    }

  tree ctor_type = NULL_TREE;
  int variant = -1;
  tree payload = NULL_TREE;
  if (crug_payload_ctor_parts (r, &ctor_type, &variant, &payload)
      && ctor_type != NULL_TREE
      && TYPE_MAIN_VARIANT (ctor_type) == type)
    {
      tree dtor = NULL_TREE;
      if (crug_payload_enum_variant_owns_p (type, variant))
	dtor = payload_transfer_dtor (ctx, env, payload, loc, type, variant);
      else
	walk_expr (ctx, env, payload, loc);

      crug_ostate st (CRUG_OWNED, NULL_TREE);
      st.variant_dtors[variant] = dtor;
      set_owned_from (env, decl, st);
      if (is_scrutinee)
	ctx.match_scrutinee_state[decl] = st;
      return;
    }

  walk_expr (ctx, env, expr, loc);
  /* An expression whose variants are not known owns nothing that the pass can
     destroy; leaving the map empty makes the leak visible later.  */
  set_owned (env, decl, NULL_TREE);
}

/* Assign the ownership state that EXPR confers on DECL.  */

static void
assign_from_expr (crug_fn_ctx &ctx, state_map &env, tree decl, tree expr,
		  location_t loc)
{
  if (owns_payload_enum_type_p (TREE_TYPE (decl)))
    {
      assign_owning_enum (ctx, env, decl, expr, loc);
      return;
    }

  if (expr == NULL_TREE || expr == error_mark_node)
    {
      set_state (env, decl, CRUG_EMPTY);
      return;
    }

  tree r = expr;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
      && decl_owns (r))
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
	  crug_ostate moved = process_move (ctx, env, CALL_EXPR_ARG (r, 0),
					    loc);
	  set_owned_from (env, decl, moved);
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

  /* A receive from a channel with an owned element hands over a value the
     caller must release.  The destructor is not part of the channel type:
     the element type erased it, so the state is owned with no known
     destructor (ADR 0025 D3/D4).  */
  if (TREE_CODE (r) == CALL_EXPR && crug_channel_recv_owned_call_p (r))
    {
      walk_expr (ctx, env, expr, loc);
      set_owned (env, decl, NULL_TREE);
      return;
    }

  /* A call whose .crugffi contract says it returns an owned value hands
     the caller that value with the destructor the sidecar names (ADR 0007
     D3).  This is the only way a checked program can take ownership from
     code that is not compiled by Crug.  */
  if (TREE_CODE (r) == CALL_EXPR)
    {
      tree ffi_dtor;
      if (crug_ffi_returns_owned (crug_call_fndecl (r), &ffi_dtor))
	{
	  walk_expr (ctx, env, expr, loc);
	  set_owned (env, decl, ffi_dtor);
	  return;
	}
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

  if (!decl_owns (r))
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
	  register_borrow (ctx, decl, src_mut, root, loc);
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
      register_borrow (ctx, decl, src_mut, root, loc);
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

  if (!decl_owns (decl))
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

static bool
is_longjmp_call (tree t)
{
  tree fn = CALL_EXPR_FN (t);
  if (fn != NULL_TREE && TREE_CODE (fn) == ADDR_EXPR)
    fn = TREE_OPERAND (fn, 0);
  if (fn == NULL_TREE || TREE_CODE (fn) != FUNCTION_DECL
      || DECL_NAME (fn) == NULL_TREE)
    return false;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fn));
  return strcmp (name, "longjmp") == 0
	 || strcmp (name, "_longjmp") == 0
	 || strcmp (name, "siglongjmp") == 0;
}

/* The policy for non-local jumps (ADR 0021 D6).  Under -fcrug-strict a
   setjmp or longjmp bypasses automatic destruction, so the call itself is an
   error outside an unsafe block; inside one the programmer takes
   responsibility and nothing is reported.  In hybrid mode the earlier
   Phase-5 warning stays: a setjmp that skips live destructors is reported
   once per owned object (crug-DROP-003).  */

static void
check_setjmp (crug_fn_ctx &ctx, state_map &env, location_t loc,
	      bool setjmp_p)
{
  if (crug_feature_enabled (CRUG_FEAT_STRICT))
    {
      if (ctx.unsafe_depth == 0)
	error_at (loc, "%qs bypasses automatic destruction; call it inside an "
		  "%<unsafe%> block under %<-fcrug-strict%> [crug-SAFE-005]",
		  setjmp_p ? "setjmp" : "longjmp");
      return;
    }

  if (!setjmp_p)
    return;

  for (state_map::const_iterator i = env.begin (); i != env.end (); ++i)
    if (i->second.state == CRUG_OWNED && i->second.dtor != NULL_TREE)
      {
	tree d = i->first;
	if (!ctx.warned_setjmp.insert (d).second)
	  continue;
	warning_at (loc, OPT_Wcrug_setjmp_drops,
		    "destructors between %<setjmp%> and %<longjmp%> may be "
		    "skipped for %qD [crug-DROP-003]", d);
      }
}

/* True when E is a borrow value: a binding declared ref or mut, or a
   borrow()/borrow_mut() call.  Nothing else carries a borrow.  */

static bool
is_borrow_value (tree e)
{
  if (e == NULL_TREE)
    return false;

  tree r = e;
  STRIP_NOPS (r);

  if ((TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
      && crug_is_borrow_decl (r, NULL))
    return true;

  if (TREE_CODE (r) == CALL_EXPR)
    {
      enum crug_intrinsic kind;
      if (is_intrinsic_call (r, &kind)
	  && (kind == CRUG_INTR_BORROW || kind == CRUG_INTR_BORROW_MUT))
	return true;
    }

  return false;
}

/* Where the borrow value E was created, for the note of crug-BORROW-019.  */

static location_t
borrow_value_origin (crug_fn_ctx &ctx, tree e)
{
  if (e == NULL_TREE)
    return UNKNOWN_LOCATION;

  tree r = e;
  STRIP_NOPS (r);

  if (TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
    {
      if (!crug_is_borrow_decl (r, NULL))
	return UNKNOWN_LOCATION;
      if (TREE_CODE (r) == PARM_DECL)
	return DECL_SOURCE_LOCATION (r);
      std::map<tree, crug_borrow>::const_iterator b = ctx.borrows.find (r);
      return b != ctx.borrows.end () ? b->second.origin
	: DECL_SOURCE_LOCATION (r);
    }

  if (TREE_CODE (r) == CALL_EXPR)
    return EXPR_HAS_LOCATION (r) ? EXPR_LOCATION (r) : UNKNOWN_LOCATION;

  return UNKNOWN_LOCATION;
}

/* The FUNCTION_DECL a call names, or NULL_TREE.  */

static tree
crug_call_fndecl (tree call)
{
  tree fn = CALL_EXPR_FN (call);
  if (fn == NULL_TREE)
    return NULL_TREE;
  tree f = fn;
  STRIP_NOPS (f);
  if (TREE_CODE (f) == ADDR_EXPR)
    f = TREE_OPERAND (f, 0);
  return TREE_CODE (f) == FUNCTION_DECL ? f : NULL_TREE;
}

/* Check the arguments of a call against its .crugffi contract (ADR 0007
   D3).  The contract is what lets checked code hand an owned value to, or
   take one from, code that is not compiled by Crug.  */

static void
check_ffi_call (crug_fn_ctx &ctx, state_map &env, tree call, location_t loc)
{
  tree fndecl = crug_call_fndecl (call);
  if (fndecl == NULL_TREE || crug_ffi_lookup (fndecl) == NULL)
    return;

  int n = call_expr_nargs (call);
  for (int i = 0; i < n; i++)
    {
      tree arg = CALL_EXPR_ARG (call, i);
      if (arg == NULL_TREE || arg == error_mark_node)
	continue;
      tree r = arg;
      STRIP_NOPS (r);

      if (crug_ffi_arg_consumes (fndecl, i))
	{
	  enum crug_intrinsic kind;
	  bool moved = (TREE_CODE (r) == CALL_EXPR
			&& is_intrinsic_call (r, &kind)
			&& kind == CRUG_INTR_MOVE);
	  bool fresh = is_allocation_call (r);
	  bool null_ok = (is_null_pointer (r)
			  && crug_ffi_arg_nullable (fndecl, i));
	  if (!moved && !fresh && !null_ok)
	    error_at (loc, "%qD takes ownership of argument %d; pass %<move%> "
		      "of an owned value, a fresh allocation, or null "
		      "[crug-FFI-007]", fndecl, i + 1);
	}

      if (crug_ffi_arg_out_owned (fndecl, i, NULL))
	{
	  tree d = NULL_TREE;
	  if (TREE_CODE (r) == ADDR_EXPR)
	    d = nop_decl (TREE_OPERAND (r, 0));
	  if (d == NULL_TREE || !crug_is_owning_decl (d))
	    error_at (loc, "%qD writes an owned value through argument %d; "
		      "pass the address of an owned variable [crug-FFI-008]",
		      fndecl, i + 1);
	  else if (state_of (env, d) != CRUG_EMPTY)
	    error_at (loc, "owned variable %qD already owns a value; drop it "
		      "or release it before passing its address for output "
		      "[crug-FFI-008]", d);
	}

      if (crug_ffi_arg_callback (fndecl, i))
	{
	  bool ok = is_null_pointer (r);
	  if (TREE_CODE (r) == FUNCTION_DECL)
	    ok = true;
	  else if (TREE_CODE (r) == ADDR_EXPR)
	    ok = (TREE_CODE (TREE_OPERAND (r, 0)) == FUNCTION_DECL);
	  if (!ok)
	    error_at (loc, "%qD calls back through argument %d; pass a "
		      "function or a null pointer [crug-FFI-009]",
		      fndecl, i + 1);
	}
    }

  /* An output parameter initializes the owned variable it is handed.  */
  for (int i = 0; i < n; i++)
    {
      tree dtor;
      if (!crug_ffi_arg_out_owned (fndecl, i, &dtor))
	continue;
      tree arg = CALL_EXPR_ARG (call, i);
      if (arg == NULL_TREE || arg == error_mark_node)
	continue;
      tree r = arg;
      STRIP_NOPS (r);
      tree d = (TREE_CODE (r) == ADDR_EXPR
		? nop_decl (TREE_OPERAND (r, 0)) : NULL_TREE);
      if (d != NULL_TREE && crug_is_owning_decl (d)
	  && state_of (env, d) == CRUG_EMPTY)
	set_owned (env, d, dtor);
    }
}

/* A borrow may only cross a call boundary through a parameter that is itself
   declared ref or mut.  This phase has no interprocedural summary (ADR 0019:
   the summary depth is zero), so every other callee is rejected rather than
   assumed to keep the referent alive.  A call whose callee is not a known
   FUNCTION_DECL fails closed.  */

/* True and sets *IS_MUT when E is a borrow whose exclusivity is known:
   a borrow binding or a borrow()/borrow_mut() call.  False for a plain
   pointer, which carries no proof of exclusive access.  */

static bool
borrow_value_mut_p (tree e, bool *is_mut)
{
  if (!is_borrow_value (e))
    return false;

  tree r = e;
  STRIP_NOPS (r);
  if (TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL)
    return crug_is_borrow_decl (r, is_mut);

  enum crug_intrinsic kind;
  if (TREE_CODE (r) == CALL_EXPR && is_intrinsic_call (r, &kind))
    {
      if (kind == CRUG_INTR_BORROW_MUT)
	{
	  *is_mut = true;
	  return true;
	}
      if (kind == CRUG_INTR_BORROW)
	{
	  *is_mut = false;
	  return true;
	}
    }
  return false;
}

/* The lender object a borrow-valued expression views, following a reborrow
   binding to its root, and whether that view is mutable.  Returns NULL_TREE
   when the expression is not a borrow or its lender is unknown.  */

static tree
borrow_arg_lender (crug_fn_ctx &ctx, tree e, bool *is_mut)
{
  if (e == NULL_TREE || e == error_mark_node)
    return NULL_TREE;

  e = STRIP_NOPS (e);
  if (TREE_CODE (e) == VAR_DECL || TREE_CODE (e) == PARM_DECL)
    {
      bool mut = false;
      if (!crug_is_borrow_decl (e, &mut))
	return NULL_TREE;
      tree lender = resolve_lender (ctx, e);
      if (is_mut != NULL)
	*is_mut = mut;
      return lender != NULL_TREE ? lender : e;
    }

  if (TREE_CODE (e) == CALL_EXPR)
    {
      enum crug_intrinsic kind;
      if (is_intrinsic_call (e, &kind)
	  && (kind == CRUG_INTR_BORROW || kind == CRUG_INTR_BORROW_MUT))
	{
	  if (is_mut != NULL)
	    *is_mut = kind == CRUG_INTR_BORROW_MUT;
	  tree lender = STRIP_NOPS (CALL_EXPR_ARG (e, 0));
	  if ((TREE_CODE (lender) == VAR_DECL
	       || TREE_CODE (lender) == PARM_DECL)
	      && crug_is_borrow_decl (lender, NULL))
	    {
	      tree root = resolve_lender (ctx, lender);
	      if (root != NULL_TREE)
		lender = root;
	    }
	  return lender;
	}
    }

  return NULL_TREE;
}

static void
check_borrow_call_args (crug_fn_ctx &ctx, tree call, location_t loc)
{
  tree fn = CALL_EXPR_FN (call);
  tree fndecl = NULL_TREE;

  if (fn != NULL_TREE)
    {
      tree f = fn;
      STRIP_NOPS (f);
      if (TREE_CODE (f) == ADDR_EXPR)
	f = TREE_OPERAND (f, 0);
      if (TREE_CODE (f) == FUNCTION_DECL)
	fndecl = f;
    }

  for (int i = 0; i < call_expr_nargs (call); i++)
    {
      if (!is_borrow_value (CALL_EXPR_ARG (call, i)))
	continue;

      bool ok = false;
      bool ffi_borrow = false;
      bool ffi_mut = false;
      if (fndecl != NULL_TREE)
	{
	  tree parm = DECL_ARGUMENTS (fndecl);
	  for (int n = 0; parm != NULL_TREE && n < i; n++)
	    parm = DECL_CHAIN (parm);
	  if (parm != NULL_TREE && TREE_CODE (parm) == PARM_DECL)
	    ok = crug_is_borrow_decl (parm, NULL);
	  /* A sidecar 'borrows' or 'mut_borrows' clause is a borrow contract
	     too (ADR 0007 D3).  */
	  if (crug_ffi_arg_borrow (fndecl, i, &ffi_mut))
	    ffi_borrow = true;
	}

      if (ok || ffi_borrow)
	{
	  /* A shared view may not be handed to a callee that may write
	     through the pointer.  */
	  if (ffi_borrow && ffi_mut)
	    {
	      tree a = CALL_EXPR_ARG (call, i);
	      STRIP_NOPS (a);
	      bool arg_mut = false;
	      enum crug_intrinsic kind;
	      if (TREE_CODE (a) == VAR_DECL || TREE_CODE (a) == PARM_DECL)
		crug_is_borrow_decl (a, &arg_mut);
	      else if (TREE_CODE (a) == CALL_EXPR
		       && is_intrinsic_call (a, &kind))
		arg_mut = (kind == CRUG_INTR_BORROW_MUT);
	      if (!arg_mut)
		{
		  error_at (loc, "a shared borrow cannot be passed to a "
			    "%<mut_borrows%> parameter [crug-BORROW-016]");
		}
	    }
	  continue;
	}

      if (fndecl != NULL_TREE)
	error_at (loc, "a borrow may only be passed to a %<ref%> or %<mut%> "
		  "parameter; %qD has no borrow contract [crug-BORROW-020]",
		  fndecl);
      else
	error_at (loc, "a borrow may only be passed to a %<ref%> or %<mut%> "
		  "parameter; the called function has no borrow contract "
		  "[crug-BORROW-020]");
    }

  /* ADR 0010: the callee treats a mut parameter as 'restrict', so the call
     must not create any other view of the same lender.  Two borrows of one
     object, or a borrow plus the lender itself, would give the callee two
     pointers it must assume do not alias.  This is checked only with
     -fcrug-alias-facts: without it the historical leniency stays.  */
  if (crug_alias_facts_enabled ())
    {
      int nargs = call_expr_nargs (call);
      for (int i = 0; i < nargs; i++)
	{
	  bool mut_i = false;
	  tree lender = borrow_arg_lender (ctx, CALL_EXPR_ARG (call, i), &mut_i);
	  if (lender == NULL_TREE)
	    continue;

	  for (int j = i + 1; j < nargs; j++)
	    {
	      bool mut_j = false;
	      tree other = borrow_arg_lender (ctx, CALL_EXPR_ARG (call, j),
					      &mut_j);
	      if (other == NULL_TREE || other != lender || (!mut_i && !mut_j))
		continue;
	      if (mut_i && mut_j)
		error_at (loc, "cannot mutably borrow %qD more than once in one "
			  "call; a mutable borrow must be exclusive "
			  "[crug-BORROW-003]", lender);
	      else
		error_at (loc, "cannot borrow %qD mutably and again in one "
			  "call; a mutable borrow must be exclusive "
			  "[crug-BORROW-002]", lender);
	    }

	  /* The lender itself (or a pointer derived from it) as another
	     argument is the same aliasing hazard.  */
	  if (!mut_i)
	    continue;
	  for (int j = 0; j < nargs; j++)
	    {
	      if (j == i)
		continue;
	      tree other = expr_base_decl (CALL_EXPR_ARG (call, j));
	      if (other != lender)
		continue;
	      tree otype = TREE_TYPE (CALL_EXPR_ARG (call, j));
	      if (otype == NULL_TREE
		  || (TREE_CODE (otype) != POINTER_TYPE
		      && TREE_CODE (otype) != ARRAY_TYPE))
		continue;
	      error_at (loc, "the lender %qD is also passed to this call; a "
			"%<mut%> parameter must be the only view of its object "
			"[crug-BORROW-023]", lender);
	    }
	}
    }

  /* ADR 0010: with -fcrug-alias-facts a 'mut' parameter is compiled as
     'restrict', so the caller must hand it a proven exclusive borrow.  A
     plain pointer proves nothing and is rejected; a shared borrow may not
     become an exclusive one.  Without the flag the historical leniency
     stays (docs/crug/BORROW.md section 2.4).  */
  if (!crug_alias_facts_enabled () || fndecl == NULL_TREE)
    return;

  for (int i = 0; i < call_expr_nargs (call); i++)
    {
      tree parm = DECL_ARGUMENTS (fndecl);
      for (int n = 0; parm != NULL_TREE && n < i; n++)
	parm = DECL_CHAIN (parm);

      bool parm_mut = false;
      if (parm == NULL_TREE || TREE_CODE (parm) != PARM_DECL
	  || !crug_is_borrow_decl (parm, &parm_mut) || !parm_mut)
	continue;

      tree arg = CALL_EXPR_ARG (call, i);
      bool arg_mut = false;
      if (!borrow_value_mut_p (arg, &arg_mut))
	error_at (loc, "passing a plain pointer to a %<mut%> parameter proves "
		  "no exclusive access; pass borrow_mut (...) or a mutable "
		  "borrow [crug-BORROW-021]");
      else if (!arg_mut)
	error_at (loc, "a shared borrow cannot be passed to a %<mut%> "
		  "parameter [crug-BORROW-022]");
    }
}

/* The declaration E names, after removing conversions, or NULL.  Used by
   the strict-mode checks, which reason about the object a pointer came
   from.  */

static tree
nop_decl (tree e)
{
  if (e == NULL_TREE || e == error_mark_node)
    return NULL_TREE;
  tree r = e;
  STRIP_NOPS (r);
  if (TREE_CODE (r) == VAR_DECL || TREE_CODE (r) == PARM_DECL
      || TREE_CODE (r) == FIELD_DECL)
    return r;
  return NULL_TREE;
}

/* True when the strict-mode checks apply at this point: -fcrug-strict is
   active and the statement is not inside an 'unsafe' block (ADR 0021 D3).  */

static bool
strict_active (crug_fn_ctx &ctx)
{
  return crug_feature_enabled (CRUG_FEAT_STRICT) && ctx.unsafe_depth == 0;
}

/* What a call may do with an owned value under -fcrug-strict: transfer it to
   an 'own' parameter through move(), view it through a 'ref' or 'mut'
   parameter, or hand it to an unmanaged parameter after release().  Anything
   else leaves checked code without an explicit act, so strict mode reports it
   (ADR 0021 D4).  */

static void
check_strict_call_args (crug_fn_ctx &ctx, tree call, location_t loc)
{
  if (!strict_active (ctx))
    return;

  tree fndecl = NULL_TREE;
  tree fn = CALL_EXPR_FN (call);
  if (fn != NULL_TREE)
    {
      tree f = fn;
      STRIP_NOPS (f);
      if (TREE_CODE (f) == ADDR_EXPR)
	f = TREE_OPERAND (f, 0);
      if (TREE_CODE (f) == FUNCTION_DECL)
	fndecl = f;
    }

  for (int i = 0; i < call_expr_nargs (call); i++)
    {
      /* A .crugffi clause is an ownership contract, so an annotated
	 callee is never "a function without an ownership contract".  */
      tree ffi_dtor;
      if (fndecl != NULL_TREE)
	{
	  bool ffi_mut;
	  if (crug_ffi_arg_borrow (fndecl, i, &ffi_mut)
	      || crug_ffi_arg_consumes (fndecl, i)
	      || crug_ffi_arg_out_owned (fndecl, i, &ffi_dtor))
	    continue;
	}

      tree d = nop_decl (CALL_EXPR_ARG (call, i));
      if (d == NULL_TREE || !crug_is_owning_decl (d))
	continue;

      tree parm = NULL_TREE;
      if (fndecl != NULL_TREE)
	{
	  parm = DECL_ARGUMENTS (fndecl);
	  for (int n = 0; parm != NULL_TREE && n < i; n++)
	    parm = DECL_CHAIN (parm);
	  if (parm != NULL_TREE && TREE_CODE (parm) != PARM_DECL)
	    parm = NULL_TREE;
	}

      if (parm != NULL_TREE && crug_is_owning_decl (parm))
	error_at (loc, "an owned value passed to an %<own%> parameter must be "
		  "moved explicitly: write move (%qD) [crug-OWN-011]", d);
      else if (parm != NULL_TREE && crug_is_borrow_decl (parm, NULL))
	/* A view: the caller keeps ownership, nothing moves.  */
	continue;
      else if (fndecl != NULL_TREE)
	error_at (loc, "an owned value passed to %qD has no ownership "
		  "contract; release (%qD) first, or use an %<unsafe%> block "
		  "[crug-OWN-012]", fndecl, d);
      else
	error_at (loc, "an owned value passed to a function without an "
		  "ownership contract; release (%qD) first, or use an "
		  "%<unsafe%> block [crug-OWN-012]", d);
    }
}

/* Strict-mode check of an explicit cast (ADR 0021 D5).  The caller has
   already established that the cast is not inside an unsafe block.  */

void
crug_strict_check_cast (location_t loc, tree to, tree from, tree operand)
{
  if (!crug_feature_enabled (CRUG_FEAT_STRICT)
      || to == NULL_TREE || from == NULL_TREE)
    return;

  tree d = nop_decl (operand);
  if (d != NULL_TREE && crug_is_owning_decl (d) && POINTER_TYPE_P (to))
    {
      error_at (loc, "an owned value cannot be converted to an unmanaged "
		"pointer; release (%qD) first, or use an %<unsafe%> block "
		"[crug-SAFE-003]", d);
      return;
    }

  bool ptr_to_int = INTEGRAL_TYPE_P (to) && POINTER_TYPE_P (from);
  bool int_to_ptr = POINTER_TYPE_P (to) && INTEGRAL_TYPE_P (from);
  if (!ptr_to_int && !int_to_ptr)
    return;

  /* The null pointer constant is an integer that names no address.  */
  if (operand != NULL_TREE && TREE_CODE (operand) == INTEGER_CST
      && integer_zerop (operand))
    return;

  error_at (loc, "conversion between an integer and a pointer outside an "
	    "%<unsafe%> block [crug-SAFE-002]");
}

/* True for an access to the artificial temporary that a lowered match uses
   for its scrutinee.  The temporary is moved into the match, so walking the
   base would report a use of a moved value; the payload transfer is modelled
   by the binding registration instead.  */

static bool
match_scrutinee_access_p (tree t)
{
  if (t == NULL_TREE || TREE_CODE (t) != COMPONENT_REF)
    return false;
  tree base = TREE_OPERAND (t, 0);
  STRIP_NOPS (base);
  return (TREE_CODE (base) == VAR_DECL || TREE_CODE (base) == PARM_DECL)
	 && match_scrutinee_p (base);
}

static void
walk_expr (crug_fn_ctx &ctx, state_map &env, tree t, location_t locus)
{
  if (t == NULL_TREE || t == error_mark_node)
    return;

  location_t loc = node_location (t, locus);

  if (match_scrutinee_access_p (t))
    return;

  switch (TREE_CODE (t))
    {
    case VAR_DECL:
    case PARM_DECL:
      check_use (ctx, env, t, loc);
      return;

    case CALL_EXPR:
      {
	enum crug_intrinsic kind;
	/* A send on a channel with an owned element transfers the value out
	   of the caller (ADR 0025 D3).  The element sits inside the compound
	   literal the parser boxed it in, and this pass does not walk a
	   compound literal, so the transfer is handled here.  */
	if (crug_channel_send_owned_call_p (t))
	  {
	    tree value = crug_channel_send_value (t);
	    if (value != NULL_TREE
		&& crug_intrinsic_call_p (value, CRUG_INTR_MOVE))
	      (void) process_move (ctx, env, CALL_EXPR_ARG (value, 0), loc);
	    walk_expr (ctx, env, CALL_EXPR_ARG (t, 0), loc);
	    return;
	  }
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
	  check_setjmp (ctx, env, loc, true);
	else if (is_longjmp_call (t))
	  check_setjmp (ctx, env, loc, false);
	check_ffi_call (ctx, env, t, loc);
	check_borrow_call_args (ctx, t, loc);
	check_strict_call_args (ctx, t, loc);
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
	/* The hidden frame of a 'go' statement is a transport object, not an
	   ordinary memory location: an owned value is moved into it and a
	   scoped borrow is captured as the pointer the task receives
	   (ADR 0024 D4).  */
	if (crug_go_frame_ref_p (lhs))
	  {
	    walk_expr (ctx, env, lhs, loc);
	    walk_expr (ctx, env, rhs, loc);
	    return;
	  }
	/* A borrow stored into an object has no automatic lifetime, so the
	   pass cannot bound it: reject the store (ADR 0019 section 2).  */
	if (is_borrow_value (rhs)
	    && (TREE_CODE (lhs) == COMPONENT_REF || TREE_CODE (lhs) == ARRAY_REF
		|| TREE_CODE (lhs) == INDIRECT_REF || TREE_CODE (lhs) == MEM_REF))
	  {
	    error_at (loc, "a borrow cannot be stored in memory; it can only "
		      "be an automatic variable [crug-BORROW-018]");
	    walk_expr (ctx, env, lhs, loc);
	    walk_expr (ctx, env, rhs, loc);
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
	    && decl_owns (lhs))
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
		   it owned and do not model the assignment.  The state is
		   EMPTY rather than MOVED so that neither the end of the scope
		   nor a later use produces a second, unrelated diagnostic.  */
		crug_ostate &s = env[lhs];
		s.state = CRUG_EMPTY;
		s.dtor = NULL_TREE;
		s.variant_dtors.clear ();
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

    case INDIRECT_REF:
    case ARRAY_REF:
    case MEM_REF:
      {
	/* Strict mode: a raw pointer may be dereferenced only inside an
	   unsafe block (ADR 0021 D2).  A raw declaration is the only way the
	   analysis knows a pointer is unchecked; pointers that arrive from
	   elsewhere are covered by the conversion rules.  */
	tree base = TREE_OPERAND (t, 0);
	tree d = nop_decl (base);
	if (d != NULL_TREE && crug_is_raw_decl (d) && strict_active (ctx))
	  error_at (loc, "dereference of %<raw%> pointer %qD outside an "
		    "%<unsafe%> block [crug-SAFE-001]", d);
	break;
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
	    && decl_owns (d))
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

  /* Phase 7: the conflict checks read the borrows that are live at this
     statement, not every borrow declared in the enclosing blocks.  A statement
     the liveness pass did not record falls back to all registered borrows, the
     lexical reading, so a gap in the model can only over-report.  */
  if (!ctx.liveness_failed)
    {
      std::map<tree, live_set>::const_iterator lb = ctx.live_before.find (t);
      if (lb != ctx.live_before.end ())
	ctx.active_borrows = lb->second;
      else
	{
	  ctx.active_borrows.clear ();
	  for (std::map<tree, crug_borrow>::const_iterator i
		 = ctx.borrows.begin (); i != ctx.borrows.end (); ++i)
	    ctx.active_borrows.insert (i->first);
	}
    }

  switch (TREE_CODE (t))
    {
    case BIND_EXPR:
      {
	size_t depth = ctx.frames.size ();
	bool unsafe_block = crug_is_unsafe_block (t);
	if (unsafe_block)
	  ctx.unsafe_depth++;
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
	/* The captures of this block's scoped tasks can be checked once the
	   block's declarations are gone: a lender declared in an inner block
	   is no longer in decl_depth at the outer scope.  */
	if (reach)
	  check_scoped_lenders (ctx, t, depth);
	pop_frame (ctx, env);
	if (unsafe_block)
	  ctx.unsafe_depth--;
	return reach;
      }

    case STATEMENT_LIST:
      {
	bool reach = true;
	tree prev = NULL_TREE;
	for (tree_stmt_iterator i = tsi_start (t); !tsi_end_p (i); tsi_next (&i))
	  {
	    tree stmt = tsi_stmt (i);
	    if (prev != NULL_TREE)
	      end_borrows_at (ctx, prev, stmt);
	    prev = stmt;
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

	/* A match arm binds one payload out of the moved scrutinee temporary.
	   The parser registered the binding; its destructor is the one the
	   temporary carried for that variant (ADR 0018 D8 R4/R5).  */
	tree bound_tmp = NULL_TREE;
	int bound_variant = -1;
	if (match_binding_reg (decl, &bound_tmp, &bound_variant)
	    && bound_tmp != NULL_TREE
	    && owns_payload_enum_type_p (TREE_TYPE (bound_tmp)))
	  {
	    crug_ostate st;
	    std::map<tree, crug_ostate>::const_iterator si
	      = ctx.match_scrutinee_state.find (bound_tmp);
	    if (si != ctx.match_scrutinee_state.end ())
	      st = si->second;
	    register_owned (ctx, env, decl);
	    std::map<int, tree>::const_iterator vi
	      = st.variant_dtors.find (bound_variant);
	    if (vi != st.variant_dtors.end ())
	      /* The scrutinee may hold this variant: the binding carries the
		 destructor the constructor or move recorded for it (possibly
		 none, when the variant owns nothing).  */
	      env[decl] = crug_ostate (CRUG_OWNED, vi->second);
	    else if (st.variant_dtors.empty ())
	      /* The set of variants the scrutinee may hold is unknown, so the
		 binding may own a payload whose destructor is unknown.  */
	      env[decl] = crug_ostate (CRUG_OWNED, NULL_TREE);
	    else
	      /* The scrutinee can hold only other variants, so this arm is
		 unreachable for it and the binding owns nothing.  */
	      set_state (env, decl, CRUG_EMPTY);
	    return true;
	  }

	if (!decl_owns (decl))
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
	  {
	    /* The return value is either the expression or, for a function
	       with a named return, an assignment to its RESULT_DECL.  */
	    tree rv = TREE_OPERAND (t, 0);
	    if (TREE_CODE (rv) == MODIFY_EXPR)
	      rv = TREE_OPERAND (rv, 1);
	    if (is_borrow_value (rv))
	      {
		location_t origin = borrow_value_origin (ctx, rv);
		error_at (loc, "a borrow cannot be returned from the function "
			  "that created it [crug-BORROW-019]");
		if (origin != UNKNOWN_LOCATION)
		  inform (origin, "borrow created here");
	      }
	    walk_expr (ctx, env, TREE_OPERAND (t, 0), loc);
	  }
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
	    /* A 'scope' block cannot be entered by a goto: the group would
	       never be created, so its children would run with no waiter and
	       the block's own destructions would have nothing to wait for.  A
	       goto out of a scope is fine, the scope block is then a common
	       prefix of the two paths and is not crossed (ADR 0024 D5).  */
	    for (size_t i = common; i < info.path.size (); i++)
	      if (crug_scope_blocks.find (info.path[i]) != crug_scope_blocks.end ()
		  && ctx.warned.insert (info.path[i]).second)
		error_at (loc, "goto jumps into a %<scope%> block [crug-CONC-015]");

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
	    /* Merge the two branches against each other, not one after the
	       other into the pre-branch map: merging then_env first would make
	       a value that both branches assign owned look half-moved when the
	       else environment is merged in afterwards.  */
	    state_map merged = then_env;
	    merge_into (ctx, merged, else_env, loc);
	    env.swap (merged);
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
      /* An asm statement can change memory that Crug tracks without the
	 analysis seeing it, so strict mode confines it to unsafe blocks
	 (ADR 0021 D7).  */
      if (strict_active (ctx))
	error_at (loc, "an %<asm%> statement outside an %<unsafe%> block can "
		  "change memory that Crug tracks [crug-SAFE-004]");
      return true;

    default:
      if (EXPR_P (t))
	{
	  enum crug_intrinsic kind;
	  if (TREE_CODE (t) == CALL_EXPR && is_intrinsic_call (t, &kind))
	    {
	      /* A discarded adopt() attaches its destructor to nothing and
		 would leak the object, so it is rejected instead of being
		 lowered away silently (ADR 0004 D10).  */
	      if (kind == CRUG_INTR_ADOPT)
		{
		  error_at (loc, "the result of %<adopt%> is discarded; "
			    "initialize an owned declaration with it "
			    "[crug-OWN-013]");
		  return true;
		}
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

/* The statement that destroys DR: a plain guarded call for an ordinary owned
   object, or one guarded call per owning variant for an owning payload enum.
   Only the variant named by the tag can own a payload, so the inactive
   variants produce no call at all.  */

static tree
drop_stmt_tree (const crug_drop &dr, location_t loc)
{
  if (dr.scope_handle != NULL_TREE)
    return crug_scope_leave_call (loc, dr.scope_handle);

  if (dr.enum_type == NULL_TREE)
    return drop_call_stmt (dr.decl, dr.dtor, loc);

  tree chain = build_empty_stmt (loc);
  for (std::map<int, tree>::const_reverse_iterator i
	 = dr.variant_dtors.rbegin ();
       i != dr.variant_dtors.rend (); ++i)
    {
      if (i->second == NULL_TREE)
	continue;
      tree tag = crug_safe_tag_ref (loc, dr.decl, dr.enum_type);
      tree payload = crug_safe_payload_ref (loc, dr.decl, dr.enum_type,
					    i->first);
      if (tag == NULL_TREE || payload == NULL_TREE)
	continue;
      tree body = drop_call_stmt (payload, i->second, loc);
      tree cond = build2 (EQ_EXPR, boolean_type_node, tag,
			  build_int_cst (TREE_TYPE (tag), i->first));
      tree stmt = build3 (COND_EXPR, void_type_node, cond, body, chain);
      SET_EXPR_LOCATION (stmt, loc);
      chain = stmt;
    }
  return chain;
}

static void
append_drops (tree &list, const std::vector<crug_drop> &drops, location_t loc)
{
  for (size_t i = 0; i < drops.size (); i++)
    append_to_statement_list (drop_stmt_tree (drops[i], loc), &list);
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
	      tree stmt = drop_stmt_tree (ed->second[i], node_location (*tp));
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
crug_check_function (tree fndecl, tree body, bool has_scope)
{
  if (fndecl == NULL_TREE || body == NULL_TREE)
    return;
  /* A function with a 'scope' statement is analysed even when only
     -fcrug-goroutines is on: the scope's wait has to be placed at every exit
     and a borrow captured by a scoped task has to be checked (Phase 15).  */
  if (!crug_ownership_enabled () && !crug_borrow_enabled () && !has_scope)
    return;

  /* An owning payload enum cannot cross a function boundary by value: the
     destructor of its payload is part of the ownership state, not of the
     type or of the C ABI, so a callee could not destroy it.  Keeping the
     value inside one function makes this limitation explicit instead of
     leaking silently (ADR 0018 D8 R5, ADR 0004 D1).  */
  for (tree a = DECL_ARGUMENTS (fndecl); a != NULL_TREE; a = DECL_CHAIN (a))
    if (TREE_CODE (a) == PARM_DECL
	&& owns_payload_enum_type_p (TREE_TYPE (a)))
      error_at (DECL_SOURCE_LOCATION (a), "an owning payload enum cannot be "
		"passed by value: the destructor of its payload is not part of "
		"the ABI; keep the value in one function or pass a pointer "
		"[crug-ADT-018]");
  if (DECL_RESULT (fndecl) != NULL_TREE)
    {
      tree restype = TREE_TYPE (DECL_RESULT (fndecl));
      if (TREE_CODE (restype) == REFERENCE_TYPE
	  && TREE_TYPE (restype) != NULL_TREE)
	restype = TREE_TYPE (restype);
      if (owns_payload_enum_type_p (restype))
	error_at (DECL_SOURCE_LOCATION (fndecl), "a function cannot return an "
		  "owning payload enum by value: the destructor of its payload "
		  "is not part of the ABI; return a pointer instead "
		  "[crug-ADT-018]");
    }

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
      b.origin = DECL_SOURCE_LOCATION (a);
      b.lender_depth = 0;
      ctx.frames[0].borrows.push_back (a);
      ctx.borrow_declared.insert (a);
      ctx.decl_depth[a] = 0;
      ctx.borrows[a] = b;
    }

  /* ADR 0010: the alias facts the function's parameters proved, as the
     restrict qualifiers the middle end reads.  */
  dump_alias_facts (fndecl);

  /* Phase 7: the last use of every borrow is computed before the checking
     walk, so the conflict checks can ask which borrows are live at a
     statement.  */
  borrow_liveness (ctx);

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

  /* The registration maps are keyed by tree nodes and the front end may
     reuse a node address after this function is done.  */
  if (crug_match_scrutinee_map != NULL)
    crug_match_scrutinee_map->clear ();
  if (crug_match_binding_map != NULL)
    crug_match_binding_map->clear ();
  /* This function's blocks have been popped.  A block that was never walked
     (error recovery) is dropped here, and so are its captures.  Blocks of an
     enclosing function stay registered: the front end analyses a nested
     function while the enclosing one is still being parsed.  */
  for (std::map<tree, tree>::iterator it = crug_scope_owners.begin ();
       it != crug_scope_owners.end (); )
    if (it->second == fndecl)
      {
	std::map<tree, tree>::iterator b = crug_scope_blocks.find (it->first);
	if (b != crug_scope_blocks.end ())
	  {
	    crug_scoped_lenders.erase (b->second);
	    crug_scope_blocks.erase (b);
	  }
	crug_scope_owners.erase (it++);
      }
    else
      ++it;
  /* The hidden frames of this function's 'go' statements are no longer
     needed: their addresses may be reused by the next function.  */
  crug_go_frames_forget (fndecl);
}
