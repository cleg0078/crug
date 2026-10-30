/* Crug send/sync properties and thread affinity (ADR 0026).

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

/* Send and Sync are properties of a C type, not of a declaration:

     Send    a value of the type may cross to another task;
     Sync    a shared borrow of the type may be used by another task.

   The inference is deliberately conservative.  A scalar, a vector, a
   function and a function pointer are both.  A channel handle is both,
   because it is the concurrency primitive itself.  An unmanaged pointer is
   neither: it names an object without saying who may touch it, and a task
   boundary would turn it into a data race.  An aggregate is both only when
   every field is, which is the same rule one level down.

   The C type system does not carry own/ref/mut: the qualifier is erased
   from the type (ADR 0003).  The crossing sites therefore translate the
   qualifier back into the property check: move (p) checks Send of the
   pointee, borrow (p) checks Sync of the pointee, borrow_mut (p) checks Send
   of the pointee.  A value of an owning payload enum is checked as the
   record it is, so the check sees its pointer payload and rejects it; the
   FFI 'send' annotation is the documented escape (ADR 0026 D6).

   The FFI metadata can override either property in both directions
   ('send'/'sync' and '!send'/'!sync', ADR 0007 D6).  A pointer whose
   pointee is annotated is classified by that annotation, because
   MASTER_SPEC section 28 describes a handle such as OpenGLContext with
   '!send !sync' while the value that moves is a pointer to it.

   Thread affinity is not a property of a type: an FFI call marked
   'thread_affinity NAME' returns a value that belongs to one thread, so the
   declaration it initializes is tainted with that name and may not cross
   (MASTER_SPEC section 28, ADR 0007).  An aggregate member that receives an
   affine value taints the object that owns it, because fields are not
   tracked separately (fix-p3, CONC-02).  A plain copy or a re-assignment of
   the object is not tracked (ADR 0026 D7).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "c-family/c-common.h"
#include "diagnostic-core.h"
#include "c-family/crug-common.h"
#include "c-family/crug-ffi.h"
#include "crug-ownership.h"
#include "crug-chan.h"
#include "crug-send-sync.h"

#include <map>
#include <string>

/* The property checks are part of the concurrency features, not a separate
   option: a task or a channel that can carry a value is exactly what makes
   Send and Sync load-bearing (ADR 0026 D1).  */

bool
crug_send_sync_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_GOROUTINES)
	 || crug_feature_enabled (CRUG_FEAT_CHANNELS);
}

/* One explicit annotation, when the sidecar has one for TYPE.
   Returns -1 for "no annotation", 0 for "not allowed", 1 for "allowed".  */

static int
ffi_override (tree type, bool want_sync)
{
  bool send = false;
  bool sync = false;
  bool not_send = false;
  bool not_sync = false;

  if (type == NULL_TREE || type == error_mark_node)
    return -1;
  if (!crug_ffi_type_flags (type, &send, &sync, &not_send, &not_sync))
    return -1;
  if (want_sync)
    {
      if (not_sync)
	return 0;
      return sync ? 1 : -1;
    }
  if (not_send)
    return 0;
  return send ? 1 : -1;
}

/* The recursive inference.  DEPTH stops a self-referential type (a record
   with a pointer to itself is rejected by the pointer rule anyway, but a
   typedef loop would otherwise not terminate).  */

static bool
type_property_1 (tree type, bool want_sync, tree *blocker, int depth)
{
  int override;

  if (type == NULL_TREE || type == error_mark_node)
    return true;
  /* A type nested deeper than this is not handed over as one object, so the
     excess depth says nothing about the value's safety; treat it as not
     sendable instead of silently accepting it (CONC-01).  */
  if (depth > 32)
    {
      if (blocker != NULL)
	*blocker = type;
      return false;
    }

  override = ffi_override (type, want_sync);
  if (override >= 0)
    return override != 0;

  if (INTEGRAL_TYPE_P (type)
      || TREE_CODE (type) == REAL_TYPE
      || TREE_CODE (type) == FIXED_POINT_TYPE
      || TREE_CODE (type) == COMPLEX_TYPE
      || TREE_CODE (type) == VECTOR_TYPE
      || TREE_CODE (type) == OFFSET_TYPE
      || TREE_CODE (type) == FUNCTION_TYPE
      || TREE_CODE (type) == METHOD_TYPE)
    return true;

  if (TREE_CODE (type) == POINTER_TYPE
      || TREE_CODE (type) == REFERENCE_TYPE)
    {
      tree target = TREE_TYPE (type);

      if (TREE_CODE (target) == FUNCTION_TYPE
	  || TREE_CODE (target) == METHOD_TYPE)
	return true;
      /* A channel handle is the concurrency primitive; two tasks sharing one
	 is the normal case (ADR 0025).  */
      if (crug_channel_type_p (type))
	return true;
      override = ffi_override (target, want_sync);
      if (override >= 0)
	return override != 0;
      if (blocker != NULL)
	*blocker = type;
      return false;
    }

  if (TREE_CODE (type) == ARRAY_TYPE)
    return type_property_1 (TREE_TYPE (type), want_sync, blocker, depth + 1);

  if (TREE_CODE (type) == RECORD_TYPE
      || TREE_CODE (type) == UNION_TYPE
      || TREE_CODE (type) == QUAL_UNION_TYPE)
    {
      tree field;

      if (!COMPLETE_TYPE_P (type))
	{
	  if (blocker != NULL)
	    *blocker = type;
	  return false;
	}
      for (field = TYPE_FIELDS (type); field != NULL_TREE;
	   field = DECL_CHAIN (field))
	{
	  if (TREE_CODE (field) != FIELD_DECL)
	    continue;
	  if (!type_property_1 (TREE_TYPE (field), want_sync, blocker,
				depth + 1))
	    return false;
	}
      return true;
    }

  /* Anything else (a void value, a type that is still incomplete) carries no
     concurrency obligation here.  */
  return true;
}

bool
crug_type_send_p (tree type, tree *blocker)
{
  return type_property_1 (type, false, blocker, 0);
}

bool
crug_type_sync_p (tree type, tree *blocker)
{
  return type_property_1 (type, true, blocker, 0);
}

/* Thread affinity.  The map is keyed by the declaration, because the
   property belongs to the object, not to its type: two handles of the same
   type can belong to different threads.  */

static std::map<tree, std::string> crug_affine_decls;

/* True when E carries an affinity: an FFI call, a declaration already
   known to be affine, or a CONSTRUCTOR with such an element.  A member
   initializer is enough for the object to inherit the property, because
   the compiler does not track fields separately (fix-p3, CONC-02).  */

static bool
affine_value_p (tree e, const char **affinity)
{
  if (e == NULL_TREE || e == error_mark_node)
    return false;

  if (TREE_CODE (e) == CONSTRUCTOR)
    {
      unsigned HOST_WIDE_INT idx;
      tree value;

      FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (e), idx, value)
	if (affine_value_p (value, affinity))
	  return true;
      return false;
    }

  return crug_expr_thread_affine_p (e, affinity);
}

void
crug_note_affine_field (tree decl, const char *affinity)
{
  if (decl == NULL_TREE || !DECL_P (decl))
    return;
  if (!crug_ffi_enabled_p ())
    return;
  if (crug_affine_decls.find (decl) != crug_affine_decls.end ())
    return;
  crug_affine_decls[decl] = std::string (affinity == NULL ? "unknown" : affinity);
}

void
crug_note_affine_store (tree lhs, tree rhs)
{
  const char *affinity = NULL;
  tree base;

  if (!crug_ffi_enabled_p () || lhs == NULL_TREE || rhs == NULL_TREE)
    return;
  if (!affine_value_p (rhs, &affinity))
    return;

  /* S.f = ... and a[i] = ... taint the object that owns the member; the
     assignment itself does not make the value portable.  */
  base = lhs;
  STRIP_NOPS (base);
  while (base != NULL_TREE
	 && (TREE_CODE (base) == COMPONENT_REF
	     || TREE_CODE (base) == ARRAY_REF))
    base = TREE_OPERAND (base, 0);
  STRIP_NOPS (base);
  if (base != NULL_TREE && DECL_P (base))
    crug_note_affine_field (base, affinity);
}

void
crug_note_affine_initializer (tree decl, tree init)
{
  tree e = init;
  tree fn;
  const char *name = NULL;

  if (decl == NULL_TREE || TREE_CODE (decl) != VAR_DECL)
    return;
  if (init == NULL_TREE || init == error_mark_node)
    return;
  if (!crug_ffi_enabled_p ())
    return;

  /* struct S s = { render_slot () }; taints s through the member.  */
  if (TREE_CODE (init) == CONSTRUCTOR)
    {
      if (affine_value_p (init, &name))
	crug_note_affine_field (decl, name);
      return;
    }

  STRIP_NOPS (e);
  if (e == NULL_TREE || TREE_CODE (e) != CALL_EXPR)
    return;
  fn = get_callee_fndecl (e);
  if (fn == NULL_TREE || !crug_ffi_thread_affine (fn, &name))
    return;
  crug_note_affine_field (decl, name);
}

bool
crug_decl_thread_affine_p (tree decl, const char **affinity)
{
  std::map<tree, std::string>::const_iterator it;

  if (decl == NULL_TREE)
    return false;
  it = crug_affine_decls.find (decl);
  if (it == crug_affine_decls.end ())
    return false;
  if (affinity != NULL)
    *affinity = it->second.c_str ();
  return true;
}

bool
crug_expr_thread_affine_p (tree expr, const char **affinity)
{
  tree e = expr;

  if (e == NULL_TREE)
    return false;
  STRIP_NOPS (e);
  if (e == NULL_TREE)
    return false;

  /* move (x), borrow (x) and borrow_mut (x) pass the value on unchanged.  */
  if (TREE_CODE (e) == CALL_EXPR
      && (crug_intrinsic_call_p (e, CRUG_INTR_MOVE)
	  || crug_intrinsic_call_p (e, CRUG_INTR_BORROW)
	  || crug_intrinsic_call_p (e, CRUG_INTR_BORROW_MUT))
      && call_expr_nargs (e) >= 1)
    return crug_expr_thread_affine_p (CALL_EXPR_ARG (e, 0), affinity);

  if (TREE_CODE (e) == CALL_EXPR)
    {
      tree fn = get_callee_fndecl (e);
      const char *name = NULL;

      if (fn != NULL_TREE && crug_ffi_thread_affine (fn, &name))
	{
	  if (affinity != NULL)
	    *affinity = name;
	  return true;
	}
      return false;
    }
  if (TREE_CODE (e) == ADDR_EXPR)
    return crug_expr_thread_affine_p (TREE_OPERAND (e, 0), affinity);
  if (DECL_P (e))
    return crug_decl_thread_affine_p (e, affinity);
  return false;
}

void
crug_forget_affine_decls (tree fndecl)
{
  std::map<tree, std::string>::iterator it = crug_affine_decls.begin ();

  if (fndecl == NULL_TREE || fndecl == error_mark_node)
    return;
  while (it != crug_affine_decls.end ())
    {
      if (DECL_P (it->first) && DECL_CONTEXT (it->first) == fndecl)
	crug_affine_decls.erase (it++);
      else
	++it;
    }
}
