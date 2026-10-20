/* switch.S - the AArch64 context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   void crug_ctx_switch (void **save_sp, void *new_sp)

   The AAPCS64 makes x19-x28, x29, x30 and the low 64 bits of v8-v15
   (d8-d15) callee-saved, and the FPCR control bits must survive a call.  The
   frame is 240 bytes:

     0..79      x19..x28
     80..95     x29, x30
     96..223    d8..d15
     224        FPCR
     232..239   padding

   After the final ret the new context runs with the stack pointer 16-byte
   aligned, as the ABI requires at a public interface.
*/

#if defined(__APPLE__)
#define SYM(x) _##x
#define TEXT_SECTION .section __TEXT,__text,regular,pure_instructions
#else
#define SYM(x) x
#define TEXT_SECTION .text
#endif

	TEXT_SECTION
	.globl	SYM(crug_ctx_switch)
	.p2align 4
SYM(crug_ctx_switch):
	sub	sp, sp, #240
	stp	x19, x20, [sp, #0]
	stp	x21, x22, [sp, #16]
	stp	x23, x24, [sp, #32]
	stp	x25, x26, [sp, #48]
	stp	x27, x28, [sp, #64]
	stp	x29, x30, [sp, #80]
	stp	d8, d9, [sp, #96]
	stp	d10, d11, [sp, #112]
	stp	d12, d13, [sp, #128]
	stp	d14, d15, [sp, #144]
	mrs	x9, fpcr
	str	x9, [sp, #224]
	mov	x9, sp
	str	x9, [x0]
	mov	sp, x1
	ldr	x9, [sp, #224]
	msr	fpcr, x9
	ldp	d8, d9, [sp, #96]
	ldp	d10, d11, [sp, #112]
	ldp	d12, d13, [sp, #128]
	ldp	d14, d15, [sp, #144]
	ldp	x29, x30, [sp, #80]
	ldp	x19, x20, [sp, #0]
	ldp	x21, x22, [sp, #16]
	ldp	x23, x24, [sp, #32]
	ldp	x25, x26, [sp, #48]
	ldp	x27, x28, [sp, #64]
	add	sp, sp, #240
	ret
