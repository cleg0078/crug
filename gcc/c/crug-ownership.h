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

/* Record DECL as owning for the current translation unit.  Reports a
   diagnostic and does not record it when the declarator cannot own.  */
extern void crug_mark_owning_decl (tree decl, location_t loc);

/* True when DECL was recorded as owning.  */
extern bool crug_is_owning_decl (tree decl);

/* The intrinsics that the C parser recognises in call position.  They are
   identifiers, never keywords, and every call is removed by the ownership
   pass, so no intrinsic reaches the middle end or the object file.  */
enum crug_intrinsic
{
  CRUG_INTR_MOVE,
  CRUG_INTR_ADOPT,
  CRUG_INTR_RELEASE,
  CRUG_INTR_DROP
};

/* Recognise ID as an intrinsic name.  Returns true and sets *KIND when it
   is one; KIND may be NULL.  */
extern bool crug_intrinsic_kind (tree id, enum crug_intrinsic *kind);

/* Build a call to one of the intrinsics.  ARG1 is NULL except for adopt.  */
extern tree crug_build_intrinsic_call (enum crug_intrinsic kind,
				       location_t loc, tree arg0, tree arg1);

/* Check BODY, the GENERIC body of FNDECL, for ownership errors, insert the
   destruction of every owned object on every exit edge, and remove the
   intrinsic calls.  */
extern void crug_check_function (tree fndecl, tree body);

#endif /* GCC_C_CRUG_OWNERSHIP_H */
