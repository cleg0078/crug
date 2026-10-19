/* stack.c - initial frames for the x86-64 System V context switch.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   crug_ctx_prepare_stack() lays out the frame that crug_ctx_switch() pops.
   The only subtlety is alignment: after the final ret, the entry function runs
   with %rsp % 16 == 8, the alignment of a function that was entered by a call.
*/

#include "ctx.h"

#include <stdint.h>

#define CRUG_CTX_MXCSR_DEFAULT 0x1f80u
#define CRUG_CTX_X87CW_DEFAULT 0x037fu

void *
crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry)
{
  uintptr_t top = ((uintptr_t) stack_top) & ~(uintptr_t) 15;
  uint64_t *sp = (uint64_t *) top;
  uint32_t *control;

  /* Padding slot: after the ret the stack pointer lands here (top - 8), which
     is 8 mod 16, the value a function entry expects.  */
  *--sp = 0;
  /* Return target: the first ret jumps here.  */
  *--sp = (uint64_t) (uintptr_t) entry;
  /* Callee-saved integer registers, in the order crug_ctx_switch pops them.  */
  *--sp = 0; /* rbx */
  *--sp = 0; /* rbp */
  *--sp = 0; /* r12 */
  *--sp = 0; /* r13 */
  *--sp = 0; /* r14 */
  *--sp = 0; /* r15 */
  /* MXCSR (4 bytes) and the x87 control word (2 bytes), then padding.  */
  *--sp = 0;
  control = (uint32_t *) sp;
  control[0] = CRUG_CTX_MXCSR_DEFAULT;
  control[1] = CRUG_CTX_X87CW_DEFAULT;

  return sp;
}
