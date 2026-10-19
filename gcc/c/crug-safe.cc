/* CRUG safe core types: slice<T>, option<T>, result<T, E>.  See crug-safe.h.

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
#include "options.h"
#include "tree.h"
#include "tree-iterator.h"
#include "input.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "langhooks.h"
#include "c-family/c-common.h"
#include "c-family/crug-common.h"
#include "diagnostic-core.h"
#include "toplev.h"
#include "c-tree.h"
#include "crug-safe.h"
#include "builtins.h"

#include <vector>

/* One instantiation.  KIND is 0 for slice, 1 for option and 2 for result.
   A and B are the type arguments.  TYPE is the record type; F1..F3 are its
   fields in declaration order (slice: ptr, len; option: tag, value;
   result: tag, union, and U is the union's own type, whose fields are
   F2 (ok) and F3 (err)).  */
struct crug_safe_entry
{
  int kind;
  tree a;
  tree b;
  tree type;
  tree f1;
  tree f2;
  tree f3;
  tree f4;
  tree u;
};

static std::vector<crug_safe_entry> *crug_safe_cache;

/* The types are built once per instantiation and kept alive by the cache.  */

static tree
crug_make_field (location_t loc, const char *name, tree type)
{
  tree field = build_decl (loc, FIELD_DECL, get_identifier (name), type);
  DECL_CONTEXT (field) = NULL_TREE;
  return field;
}

/* Build a RECORD_TYPE (or UNION_TYPE) whose TYPE_FIELDS are FIELDS, in
   declaration order.  The fields are chained in that order.  */

static tree
crug_build_aggregate (bool is_union, const char *name, tree fields,
		      location_t loc)
{
  tree type = lang_hooks.types.make_type (is_union ? UNION_TYPE : RECORD_TYPE);
  tree prev = NULL_TREE;

  for (tree f = fields; f != NULL_TREE; f = DECL_CHAIN (f))
    {
      DECL_FIELD_CONTEXT (f) = type;
      if (prev != NULL_TREE)
	DECL_CHAIN (prev) = f;
      else
	TYPE_FIELDS (type) = f;
      prev = f;
    }
  if (prev != NULL_TREE)
    DECL_CHAIN (prev) = NULL_TREE;

  layout_type (type);
  TYPE_NAME (type) = build_decl (loc, TYPE_DECL, get_identifier (name), type);
  TYPE_STUB_DECL (type) = TYPE_NAME (type);
  layout_decl (TYPE_NAME (type), 0);
  return type;
}

bool
crug_slices_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_SLICES);
}

bool
crug_result_enabled (void)
{
  return crug_feature_enabled (CRUG_FEAT_RESULT);
}

static struct crug_safe_entry *
crug_lookup_entry (tree type)
{
  if (crug_safe_cache == NULL)
    return NULL;
  for (size_t i = 0; i < crug_safe_cache->size (); i++)
    if ((*crug_safe_cache)[i].type == type)
      return &(*crug_safe_cache)[i];
  return NULL;
}

tree
crug_slice_type (tree element, location_t loc)
{
  if (crug_safe_cache != NULL)
    for (size_t i = 0; i < crug_safe_cache->size (); i++)
      {
	struct crug_safe_entry &e = (*crug_safe_cache)[i];
	if (e.kind == 0 && e.a == element)
	  return e.type;
      }

  tree ptr_type = build_pointer_type (element);
  tree ptr_field = crug_make_field (loc, "ptr", ptr_type);
  tree len_field = crug_make_field (loc, "len", size_type_node);
  DECL_CHAIN (ptr_field) = len_field;

  tree type = crug_build_aggregate (false, "slice", ptr_field, loc);

  struct crug_safe_entry e;
  e.kind = 0;
  e.a = element;
  e.b = NULL_TREE;
  e.type = type;
  e.f1 = ptr_field;
  e.f2 = len_field;
  e.f3 = NULL_TREE;
  e.f4 = NULL_TREE;
  e.u = NULL_TREE;
  if (crug_safe_cache == NULL)
    crug_safe_cache = new std::vector<crug_safe_entry> ();
  crug_safe_cache->push_back (e);
  return type;
}

tree
crug_option_type (tree payload, location_t loc)
{
  if (crug_safe_cache != NULL)
    for (size_t i = 0; i < crug_safe_cache->size (); i++)
      {
	struct crug_safe_entry &e = (*crug_safe_cache)[i];
	if (e.kind == 1 && e.a == payload)
	  return e.type;
      }

  tree tag_field = crug_make_field (loc, "some", boolean_type_node);
  tree value_field = crug_make_field (loc, "value", payload);
  DECL_CHAIN (tag_field) = value_field;

  tree type = crug_build_aggregate (false, "option", tag_field, loc);

  struct crug_safe_entry e;
  e.kind = 1;
  e.a = payload;
  e.b = NULL_TREE;
  e.type = type;
  e.f1 = tag_field;
  e.f2 = value_field;
  e.f3 = NULL_TREE;
  e.f4 = NULL_TREE;
  e.u = NULL_TREE;
  if (crug_safe_cache == NULL)
    crug_safe_cache = new std::vector<crug_safe_entry> ();
  crug_safe_cache->push_back (e);
  return type;
}

tree
crug_result_type (tree ok_type, tree err_type, location_t loc)
{
  if (crug_safe_cache != NULL)
    for (size_t i = 0; i < crug_safe_cache->size (); i++)
      {
	struct crug_safe_entry &e = (*crug_safe_cache)[i];
	if (e.kind == 2 && e.a == ok_type && e.b == err_type)
	  return e.type;
      }

  tree ok_field = crug_make_field (loc, "value", ok_type);
  tree err_field = crug_make_field (loc, "error", err_type);
  DECL_CHAIN (ok_field) = err_field;
  tree union_type = crug_build_aggregate (true, "result_payload", ok_field, loc);

  tree tag_field = crug_make_field (loc, "ok", boolean_type_node);
  tree union_field = crug_make_field (loc, "u", union_type);
  DECL_CHAIN (tag_field) = union_field;

  tree type = crug_build_aggregate (false, "result", tag_field, loc);

  struct crug_safe_entry e;
  e.kind = 2;
  e.a = ok_type;
  e.b = err_type;
  e.type = type;
  e.f1 = tag_field;
  e.f2 = union_field;
  e.f3 = ok_field;
  e.f4 = err_field;
  e.u = union_type;
  if (crug_safe_cache == NULL)
    crug_safe_cache = new std::vector<crug_safe_entry> ();
  crug_safe_cache->push_back (e);
  return type;
}

bool
crug_slice_type_p (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 0;
}

bool
crug_option_type_p (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 1;
}

bool
crug_result_type_p (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 2;
}

bool
crug_safe_type_p (tree type)
{
  return crug_lookup_entry (type) != NULL;
}

tree
crug_slice_ptr_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 0 ? e->f1 : NULL_TREE;
}

tree
crug_slice_len_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 0 ? e->f2 : NULL_TREE;
}

tree
crug_option_tag_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 1 ? e->f1 : NULL_TREE;
}

tree
crug_option_value_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 1 ? e->f2 : NULL_TREE;
}

tree
crug_result_tag_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 2 ? e->f1 : NULL_TREE;
}

tree
crug_result_union_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 2 ? e->f2 : NULL_TREE;
}

tree
crug_result_ok_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 2 ? e->f3 : NULL_TREE;
}

tree
crug_result_err_field (tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  return e != NULL && e->kind == 2 ? e->f4 : NULL_TREE;
}

/* Build a COMPONENT_REF for FIELD inside BASE.  */

static tree
crug_component_ref (location_t loc, tree base, tree field)
{
  tree ref;

  if (field == NULL_TREE || base == error_mark_node)
    return error_mark_node;
  ref = build3 (COMPONENT_REF, TREE_TYPE (field), base, field, NULL_TREE);
  SET_EXPR_LOCATION (ref, loc);
  TREE_SIDE_EFFECTS (ref) = TREE_SIDE_EFFECTS (base);
  return ref;
}

/* Build a CONSTRUCTOR for TYPE from two field/value pairs.  An elt whose
   FIELD is NULL_TREE is skipped.  */

static tree
crug_constructor (tree type, tree f1, tree v1, tree f2, tree v2)
{
  vec<constructor_elt, va_gc> *v = NULL;

  if (f1 != NULL_TREE)
    CONSTRUCTOR_APPEND_ELT (v, f1, v1);
  if (f2 != NULL_TREE)
    CONSTRUCTOR_APPEND_ELT (v, f2, v2);
  return build_constructor (type, v);
}

/* Turn a constructor into an expression of TYPE.  */

static tree
crug_constructor_expr (location_t loc, tree type, tree ctor)
{
  tree literal;

  if (type == error_mark_node || ctor == error_mark_node)
    return error_mark_node;
  CONSTRUCTOR_NON_CONST (ctor) = 1;
  /* NON_CONST false: build_compound_literal otherwise wraps the literal in
     a C_MAYBE_CONST_EXPR, which generic gimplify cannot handle.  */
  literal = build_compound_literal (loc, type, ctor, false, 0, NULL);
  if (literal != error_mark_node)
    SET_EXPR_LOCATION (literal, loc);
  return literal;
}

tree
crug_safe_option_ctor (location_t loc, tree type, int variant, tree payload)
{
  tree tag_field = crug_option_tag_field (type);
  tree value_field = crug_option_value_field (type);
  tree ctor;

  if (tag_field == NULL_TREE)
    return error_mark_node;
  if (variant == CRUG_VARIANT_SOME)
    {
      if (payload == NULL_TREE)
	return error_mark_node;
      ctor = crug_constructor (type, tag_field,
			       build_int_cst (boolean_type_node, 1),
			       value_field, payload);
    }
  else
    ctor = crug_constructor (type, tag_field,
			     build_int_cst (boolean_type_node, 0),
			     NULL_TREE, NULL_TREE);
  return crug_constructor_expr (loc, type, ctor);
}

tree
crug_safe_result_ctor (location_t loc, tree type, int variant, tree payload)
{
  tree tag_field = crug_result_tag_field (type);
  tree union_field = crug_result_union_field (type);
  tree member = variant == CRUG_VARIANT_OK
		? crug_result_ok_field (type) : crug_result_err_field (type);
  tree union_value, ctor;

  if (tag_field == NULL_TREE || payload == NULL_TREE)
    return error_mark_node;
  union_value = crug_constructor (TREE_TYPE (union_field), member, payload,
				  NULL_TREE, NULL_TREE);
  CONSTRUCTOR_NON_CONST (union_value) = 1;
  ctor = crug_constructor (type, tag_field,
			   build_int_cst (boolean_type_node,
					  variant == CRUG_VARIANT_OK ? 1 : 0),
			   union_field, union_value);
  return crug_constructor_expr (loc, type, ctor);
}

tree
crug_safe_tag_ref (location_t loc, tree base, tree type)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  if (e == NULL)
    return error_mark_node;
  return crug_component_ref (loc, base, e->f1);
}

tree
crug_safe_payload_ref (location_t loc, tree base, tree type, int variant)
{
  struct crug_safe_entry *e = crug_lookup_entry (type);
  tree field;

  if (e == NULL)
    return error_mark_node;
  if (e->kind == 1)
    {
      if (variant != CRUG_VARIANT_SOME)
	return NULL_TREE;
      return crug_component_ref (loc, base, e->f2);
    }
  if (e->kind == 2)
    {
      tree u = crug_component_ref (loc, base, e->f2);
      if (u == error_mark_node)
	return error_mark_node;
      field = variant == CRUG_VARIANT_OK ? e->f3 : e->f4;
      return crug_component_ref (loc, u, field);
    }
  return NULL_TREE;
}

/* Build a slice<T> value from a pointer and a length (ADR 0018 D7).  The
   slice is a borrowed view: it never owns the pointee.  */

tree
crug_safe_slice_ctor (location_t loc, tree ptr, tree len)
{
  tree ptr_type, type, ctor;

  if (ptr == error_mark_node || len == error_mark_node)
    return error_mark_node;
  ptr_type = TREE_TYPE (ptr);
  if (ptr_type != NULL_TREE && TREE_CODE (ptr_type) == ARRAY_TYPE)
    {
      /* An array decays to a pointer to its first element.  */
      tree elem = TREE_TYPE (ptr_type);
      tree addr = build1 (ADDR_EXPR, build_pointer_type (elem),
			  build_array_ref (loc, ptr,
					   build_int_cst (size_type_node, 0)));
      ptr = addr;
      ptr_type = TREE_TYPE (ptr);
    }
  if (ptr_type == NULL_TREE || TREE_CODE (ptr_type) != POINTER_TYPE)
    {
      error_at (loc, "%<slice%> needs a pointer as its first argument "
		"[crug-TYPE-012]");
      return error_mark_node;
    }

  type = crug_slice_type (TREE_TYPE (ptr_type), loc);
  if (type == error_mark_node)
    return error_mark_node;
  ctor = crug_constructor (type, crug_slice_ptr_field (type), ptr,
			   crug_slice_len_field (type),
			   convert (size_type_node, len));
  return crug_constructor_expr (loc, type, ctor);
}

/* Slice indexing: s[i].  With bounds checks on, the trap is embedded in the
   pointer operand of the array reference,
     ((i >= s.len) ? __builtin_trap () : (void) 0, s.ptr)[i],
   so the reference is still an lvalue and 's[i] = v' works, and the index
   is evaluated once (SAVE_EXPR) even though it appears in the check and in
   the reference.  The comparison is unsigned, so a negative index traps as
   well.  */

tree
crug_safe_slice_index (location_t loc, tree slice, tree index)
{
  tree type = TREE_TYPE (slice);
  struct crug_safe_entry *e = crug_lookup_entry (type);
  tree base, ptr_ref, len_ref, ptr_type, idx, cmp, check, checked_ptr;

  if (e == NULL || e->kind != 0)
    return error_mark_node;

  if (!flag_crug_bounds_checks)
    {
      ptr_ref = crug_component_ref (loc, slice, e->f1);
      if (ptr_ref == error_mark_node)
	return error_mark_node;
      return build_array_ref (loc, ptr_ref, index);
    }

  base = save_expr (slice);
  ptr_ref = crug_component_ref (loc, base, e->f1);
  len_ref = crug_component_ref (loc, base, e->f2);
  if (ptr_ref == error_mark_node || len_ref == error_mark_node)
    return error_mark_node;

  ptr_type = TREE_TYPE (ptr_ref);
  idx = save_expr (index);
  /* Direct tree so no C_MAYBE_CONST_EXPR reaches generic gimplify.  */
  cmp = build2 (GE_EXPR, boolean_type_node,
		convert (size_type_node, idx), len_ref);
  check = build3 (COND_EXPR, void_type_node, cmp,
		  build_call_expr_loc (loc,
				       builtin_decl_explicit (BUILT_IN_TRAP),
				       0),
		  void_node);
  SET_EXPR_LOCATION (check, loc);
  checked_ptr = build2 (COMPOUND_EXPR, ptr_type, check, ptr_ref);
  SET_EXPR_LOCATION (checked_ptr, loc);
  return build_array_ref (loc, checked_ptr, idx);
}
