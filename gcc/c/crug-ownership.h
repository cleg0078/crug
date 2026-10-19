/* Crug ownership and drop analysis.
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

#ifndef GCC_C_CRUG_OWNERSHIP_H
#define GCC_C_CRUG_OWNERSHIP_H

/* True when -fcrug-ownership (or a flag that implies it) is active.  */
extern bool crug_ownership_enabled (void);

/* True when -fcrug-borrow (or a flag that implies it) is active.  */
extern bool crug_borrow_enabled (void);

/* Record DECL as owning for the current translation unit.  Reports a
   diagnostic and does not record it when the declarator cannot own.  */
extern void crug_mark_owning_decl (tree decl, location_t loc);

/* True when DECL was recorded as owning.  */
extern bool crug_is_owning_decl (tree decl);

/* Record DECL as a raw (unchecked) pointer for the current translation
   unit.  A raw pointer carries no ownership and no borrow; in strict mode
   it may be dereferenced only inside an unsafe block (ADR 0021).  */
extern void crug_mark_raw_decl (tree decl, location_t loc);

/* True when DECL was recorded as raw.  */
extern bool crug_is_raw_decl (tree decl);

/* Mark the compound statement of an 'unsafe { }' block, so that the
   strict-mode checks stand down inside it.  The mark is analysis metadata:
   it costs nothing at run time (RFC-0001 section 20).  */
extern void crug_mark_unsafe_block (tree bind);

/* True when BIND is the compound statement of an unsafe block.  */
extern bool crug_is_unsafe_block (tree bind);

/* True when -fcrug-strict (or a flag that implies it) is active.  */
extern bool crug_strict_enabled (void);

/* Strict-mode check of an explicit cast of OPERAND to TO, whose source type
   is FROM.  Reports crug-SAFE-002 for a conversion between an integer and a
   pointer and crug-SAFE-003 for an owned value converted to an unmanaged
   pointer.  The caller is responsible for the unsafe-block test.  */
extern void crug_strict_check_cast (location_t loc, tree to, tree from,
				    tree operand);

/* Record DECL as a borrow binding.  IS_MUT selects an exclusive borrow.
   Reports a diagnostic and does not record it when the declarator cannot
   hold a borrow.  */
extern void crug_mark_borrow_decl (tree decl, bool is_mut, location_t loc);

/* True when DECL is a borrow binding.  When IS_MUT is not NULL it receives
   whether the borrow is exclusive.  */
extern bool crug_is_borrow_decl (tree decl, bool *is_mut);

/* The intrinsics that the C parser recognises in call position.  They are
   identifiers, never keywords, and every call is removed by the ownership
   pass, so no intrinsic reaches the middle end or the object file.  */
enum crug_intrinsic
{
  CRUG_INTR_MOVE,
  CRUG_INTR_ADOPT,
  CRUG_INTR_RELEASE,
  CRUG_INTR_DROP,
  CRUG_INTR_BORROW,
  CRUG_INTR_BORROW_MUT
};

/* Recognise ID as an intrinsic name.  Returns true and sets *KIND when it
   is one; KIND may be NULL.  Name recognition is independent of the feature
   flags; crug_intrinsic_enabled decides whether the intrinsic may be
   called.  */
extern bool crug_intrinsic_kind (tree id, enum crug_intrinsic *kind);

/* True when the feature set that owns KIND is active.  */
extern bool crug_intrinsic_enabled (enum crug_intrinsic kind);

/* Build a call to one of the intrinsics.  ARG1 is NULL except for adopt.  */
extern tree crug_build_intrinsic_call (enum crug_intrinsic kind,
				       location_t loc, tree arg0, tree arg1);

/* A match statement lowered by the parser (ADR 0018 D8 R4/R5) hides an
   artificial temporary: TMP holds the moved scrutinee and each arm binds one
   payload out of it.  The parser registers the temporary and every binding so
   that the ownership pass can transfer the payload's destructor to the
   binding and treat the temporary as consumed.  */
extern void crug_register_match_scrutinee (tree tmp);
extern void crug_register_match_binding (tree binding, tree tmp, int variant);

/* Check BODY, the GENERIC body of FNDECL, for ownership errors, insert the
   destruction of every owned object on every exit edge, and remove the
   intrinsic calls.  */
extern void crug_check_function (tree fndecl, tree body);

#endif /* GCC_C_CRUG_OWNERSHIP_H */
