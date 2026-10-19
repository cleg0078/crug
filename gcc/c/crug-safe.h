/* CRUG safe core types: slice<T>, option<T> and result<T, E>.

   These types are Crug-native: they have no C ABI until the layout is frozen in
   docs/crug/ABI.md (ADR 0006 D5/D6).  They are built as ordinary RECORD_TYPEs
   with named fields, so the C front end, codegen and the debugger see plain
   aggregates and no runtime support is needed.

   The parser builds them in type position and constructs values with
   build_compound_literal, so a constructor is an ordinary aggregate initializer
   and lowers to plain stores.

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

#ifndef GCC_CRUG_SAFE_H
#define GCC_CRUG_SAFE_H

/* True when -fcrug-slices is active.  */
extern bool crug_slices_enabled (void);

/* True when -fcrug-result is active.  */
extern bool crug_result_enabled (void);

/* Type constructors.  Each returns a canonical RECORD_TYPE for the
   instantiation, building it on first use.  LOC is used for the type's
   declarations.  */
extern tree crug_slice_type (tree element, location_t loc);
extern tree crug_option_type (tree payload, location_t loc);
extern tree crug_result_type (tree ok_type, tree err_type, location_t loc);

/* Recognisers.  */
extern bool crug_slice_type_p (tree type);
extern bool crug_option_type_p (tree type);
extern bool crug_result_type_p (tree type);

/* Field access.  All return NULL_TREE for a type that is not of the
   corresponding kind.  */
extern tree crug_slice_ptr_field (tree type);
extern tree crug_slice_len_field (tree type);
extern tree crug_option_tag_field (tree type);
extern tree crug_option_value_field (tree type);
extern tree crug_result_tag_field (tree type);
extern tree crug_result_ok_field (tree type);
extern tree crug_result_err_field (tree type);
extern tree crug_result_union_field (tree type);

/* Number of variants of an option (none, some) or result (err, ok)
   is two; these name the two tags in declaration order.  */
enum crug_variant
{
  CRUG_VARIANT_NONE = 0,
  CRUG_VARIANT_SOME = 1,
  CRUG_VARIANT_ERR = 0,
  CRUG_VARIANT_OK = 1
};

/* Construct a value of TYPE.  PAYLOAD is NULL_TREE for none.  Returns a
   COMPOUND_LITERAL_EXPR, or error_mark_node.  */
extern tree crug_safe_option_ctor (location_t loc, tree type, int variant,
				   tree payload);
extern tree crug_safe_result_ctor (location_t loc, tree type, int variant,
				   tree payload);

/* Build a reference to the tag of BASE, an lvalue of TYPE.  */
extern tree crug_safe_tag_ref (location_t loc, tree base, tree type);

/* Build a reference to the payload of variant VARIANT of BASE.  Returns
   NULL_TREE when the variant has no payload.  */
extern tree crug_safe_payload_ref (location_t loc, tree base, tree type,
				   int variant);

/* Build a slice<T> from a pointer and a length.  Error and error_mark_node
   when PTR is not a pointer (crug-TYPE-012).  */
extern tree crug_safe_slice_ctor (location_t loc, tree ptr, tree len);

/* Bounds-checked slice indexing: s[i].  Returns error_mark_node when SLICE
   is not a slice value.  */
extern tree crug_safe_slice_index (location_t loc, tree slice, tree index);

/* True when TYPE is one of the Crug-native core types.  */
extern bool crug_safe_type_p (tree type);

/* A payload enum (ADR 0018 D8): 'enum Message { Text (char *), Quit };'.
   One variant of its definition, while the body is parsed.  */
struct crug_payload_variant_spec
{
  tree name;		/* the variant name, an IDENTIFIER_NODE.  */
  tree payload;		/* the payload type, NULL_TREE for a unit variant.  */
  bool owning;		/* the payload type-name was written 'own'.  */
};

/* Build the record type that represents a payload enum.  TAG_NAME is the
   enum tag, or NULL_TREE for an anonymous definition.  The variants are in
   declaration order; the tag value of a variant is its index.  */
extern tree crug_payload_enum_type (location_t loc, tree tag_name,
				    const struct crug_payload_variant_spec
				    *variants, int nvariants);

extern bool crug_payload_enum_type_p (tree type);
extern tree crug_payload_enum_tag_field (tree type);
extern tree crug_payload_enum_union_field (tree type);
extern int crug_payload_enum_variant_count (tree type);
extern bool crug_payload_enum_variant_has_payload (tree type, int variant);
/* True when the payload type-name of VARIANT was written 'own'.  The 'own'
   qualifier is not part of the payload type (it is erased from the type like
   every other Crug qualifier), so this is the only way to know that the
   payload owns its value.  */
extern bool crug_payload_enum_variant_owns_p (tree type, int variant);
extern bool crug_payload_enum_owns_p (tree type);
extern tree crug_payload_enum_payload_field (tree type, int variant);
extern const char *crug_payload_enum_variant_name (tree type, int variant);
extern int crug_payload_enum_variant_by_name (tree type, const char *name);

/* Construct a value of variant VARIANT of the payload enum TYPE.  PAYLOAD is
   NULL_TREE for a unit variant.  */
extern tree crug_payload_enum_ctor (location_t loc, tree type, int variant,
				    tree payload);

/* Decompose a payload-enum constructor expression, which is the
   COMPOUND_LITERAL_EXPR that crug_payload_enum_ctor returns.  Store the
   payload enum type, the variant index and the payload expression (NULL_TREE
   for a unit variant) and return true; return false when EXPR is not such a
   constructor.  The ownership pass needs this because a compound literal
   hides its initializer behind a DECL_EXPR, which is not an expression.  */
extern bool crug_payload_ctor_parts (tree expr, tree *type, int *variant,
				     tree *payload);

/* Record that VARIANT_DECL, the CONST_DECL bound for the variant name, names
   variant VARIANT of the payload enum TYPE.  */
extern void crug_payload_enum_bind_variant (tree variant_decl, tree type,
					    int variant);

/* If VARIANT_DECL is a payload-enum variant, fill in TYPE, VARIANT and
   HAS_PAYLOAD and return true.  */
extern bool crug_payload_enum_variant_decl (tree variant_decl, tree *type,
					    int *variant, bool *has_payload);

/* The tag of this name declared in the current scope, or NULL_TREE.  A
   payload enum cannot complete a forward 'enum' reference, because its tag
   denotes a record type (ADR 0018 D8).  */
extern tree crug_lookup_current_tag (enum tree_code code, tree name,
				     location_t *ploc);

#endif /* GCC_CRUG_SAFE_H */