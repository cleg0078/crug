/* switch.S - the x86-64 System V context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   void crug_ctx_switch (void **save_sp, void *new_sp)

   The System V AMD64 ABI makes %rbx, %rbp, %r12, %r13, %r14 and %r15
   callee-saved, and the MXCSR control bits plus the x87 control word survive
   calls.  All of them are saved here, so a task is transparent to the C ABI:
   from the compiler's point of view the task's own stack is just another call
   frame.  The 8-byte slot holds MXCSR (4 bytes) and the x87 control word
   (2 bytes) and pads to 8, which keeps the stack 16-byte aligned.

   The layout prepared by crug_ctx_prepare_stack, from the stack pointer up:

     [mxcsr|cw] [r15] [r14] [r13] [r12] [rbp] [rbx] [return address] [pad]

   After the final ret the new context runs with %rsp % 16 == 8, exactly as a
   function entered by a call instruction expects.
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
#ifndef __APPLE__
	.type	SYM(crug_ctx_switch), @function
#endif
	.align	16
SYM(crug_ctx_switch):
	.cfi_startproc
	pushq	%rbx
	pushq	%rbp
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	subq	$8, %rsp
	stmxcsr	(%rsp)
	fnstcw	4(%rsp)
	movq	%rsp, (%rdi)
	movq	%rsi, %rsp
	ldmxcsr	(%rsp)
	fldcw	4(%rsp)
	addq	$8, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbp
	popq	%rbx
	ret
	.cfi_endproc
#ifndef __APPLE__
	.size	SYM(crug_ctx_switch), .-SYM(crug_ctx_switch)

	.section	.note.GNU-stack, "", @progbits
#endif
