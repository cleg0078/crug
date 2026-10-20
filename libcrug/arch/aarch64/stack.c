/* stack.c - initial frames for the AArch64 context switch.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The frame is the 240-byte one switch.S pops.  x30 carries the entry
   address, so the final ret jumps into the task; every callee-saved register
   starts at zero and FPCR at its reset value.
*/

#include "ctx.h"

#include <stdint.h>

void *
crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry)
{
  uintptr_t top = ((uintptr_t) stack_top) & ~(uintptr_t) 15;
  unsigned char *base = (unsigned char *) (top - 240);
  uint64_t *words = (uint64_t *) base;

  for (unsigned i = 0; i < 30; i++)
    words[i] = 0;
  /* x29 (frame pointer) stays zero; x30 is the entry address.  */
  words[10] = 0;
  words[11] = (uint64_t) (uintptr_t) entry;
  /* d8..d15 and FPCR stay zero.  */

  return base;
}
