/* ctx.h - the x86-64 Windows context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   A Windows x86-64 port keeps the same interface as the System V one (ADR
   0013 D1): the scheduler core only ever sees crug_ctx_switch() and
   crug_ctx_prepare_stack().  What differs is the register set the Microsoft
   x64 ABI makes nonvolatile: rbx, rbp, rdi, rsi, r12-r15 and xmm6-xmm15, plus
   MXCSR control bits and the x87 control word.
*/

#ifndef CRUG_CTX_H
#define CRUG_CTX_H

#if !defined(_WIN32) || !defined(__x86_64__)
#error "this context switch is for x86-64 Windows only; pick another backend"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*crug_ctx_entry) (void);

/* Save the running context through save_sp and continue on new_sp.  */
void crug_ctx_switch (void **save_sp, void *new_sp);

/* Build the initial frame of a new context: the first switch into it enters
   entry with a valid stack and the Windows x64 ABI state.  */
void *crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_CTX_H */
