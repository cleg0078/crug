/* Crug send/sync properties and thread affinity (ADR 0026).  See
   crug-send-sync.cc.

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

#ifndef GCC_C_CRUG_SEND_SYNC_H
#define GCC_C_CRUG_SEND_SYNC_H

/* True when the concurrency checks are active: -fcrug-goroutines or
   -fcrug-channels is in effect.  There is no separate option: a task or a
   channel that can carry a value is exactly what makes the property
   load-bearing (ADR 0026 D1).  */
extern bool crug_send_sync_enabled (void);

/* Send: a value of TYPE may cross to another task.  Sync: a shared borrow
   of TYPE may be used by another task.  Both properties are inferred over
   the C type, and the FFI metadata can state either one explicitly
   (ADR 0007 D6).  On failure *BLOCKER, when it is not NULL, receives the
   first component that lacks the property.  */
extern bool crug_type_send_p (tree type, tree *blocker);
extern bool crug_type_sync_p (tree type, tree *blocker);

/* Thread affinity: a declaration initialized from an FFI call marked
   'thread_affinity NAME' belongs to that thread and may not migrate to a
   task or a channel (MASTER_SPEC section 28, ADR 0007).  */
extern void crug_note_affine_initializer (tree decl, tree init);
extern bool crug_decl_thread_affine_p (tree decl, const char **affinity);
extern bool crug_expr_thread_affine_p (tree expr, const char **affinity);

/* Mark DECL as thread-affine because one of its aggregate members received
   an affine value.  Fields are not tracked separately: the whole object
   inherits the property, so it cannot cross a worker boundary either
   (fix-p3, CONC-02).  */
extern void crug_note_affine_field (tree decl, const char *affinity);

/* Record the affinity an assignment LHS = RHS puts on the object behind
   LHS, when RHS is affine or a CONSTRUCTOR with an affine member.  */
extern void crug_note_affine_store (tree lhs, tree rhs);

/* Drop the affinity records of the declarations of FNDECL once its body
   has been parsed.  */
extern void crug_forget_affine_decls (tree fndecl);

#endif /* GCC_C_CRUG_SEND_SYNC_H */
