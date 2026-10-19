/* ctx.h - the x86-64 System V context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The switch lives under libcrug/arch/, never in libcrug/core (ADR 0013):
   core reaches it through these two functions only.

   crug_ctx_switch() saves the callee-saved integer registers and the
   floating-point control words of the current context, stores the stack
   pointer through save_sp, loads new_sp and returns into the other context.
   crug_ctx_prepare_stack() builds the initial frame of a new context so that
   the first switch returns into entry with the stack aligned as a C function
   expects.
*/

#ifndef CRUG_CTX_H
#define CRUG_CTX_H

#include <stddef.h>

#if !defined(__x86_64__) || defined(_WIN32)
#error "the Phase 14 context switch supports x86-64 System V only; other ports are Phase 18"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*crug_ctx_entry) (void);

void crug_ctx_switch (void **save_sp, void *new_sp);
void *crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_CTX_H */
