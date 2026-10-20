/* ctx.h - the AArch64 context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The same interface as every other port (ADR 0013 D1): the scheduler core
   only sees crug_ctx_switch() and crug_ctx_prepare_stack().  AArch64 keeps
   x19-x28, the frame pointer x29, the link register x30 and the low 64 bits
   of v8-v15 (d8-d15) across a call, plus the floating-point control register.
*/

#ifndef CRUG_CTX_H
#define CRUG_CTX_H

#if !defined(__aarch64__)
#error "this context switch is for AArch64 only; pick another arch directory"
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
