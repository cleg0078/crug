/* stack.c - initial frames for the x86-64 Windows context switch.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   crug_ctx_prepare_stack() lays out the 240-byte frame that
   crug_ctx_switch() pops; see switch.S for the layout.  The frame starts 256
   bytes below the 16-byte-aligned stack top: 240 bytes of saved state, the
   return address, and one padding slot, so that after the final ret the entry
   function runs with %rsp % 16 == 8.
*/

#include "ctx.h"

#include <stdint.h>

#define CRUG_CTX_MXCSR_DEFAULT 0x1f80u
#define CRUG_CTX_X87CW_DEFAULT 0x037fu
#define CRUG_CTX_FRAME 240u

void *
crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry)
{
  uintptr_t top = ((uintptr_t) stack_top) & ~(uintptr_t) 15;
  unsigned char *base = (unsigned char *) (top - CRUG_CTX_FRAME - 16);
  uint32_t *control;

  /* xmm6..xmm15 and the general registers start at zero.  */
  for (unsigned i = 0; i < CRUG_CTX_FRAME; i++)
    base[i] = 0;

  control = (uint32_t *) (base + 160);
  control[0] = CRUG_CTX_MXCSR_DEFAULT;
  control[1] = CRUG_CTX_X87CW_DEFAULT;

  /* Return address, then the padding slot that makes %rsp % 16 == 8.  */
  *(uint64_t *) (base + 240) = (uint64_t) (uintptr_t) entry;
  *(uint64_t *) (base + 248) = 0;

  return base;
}
