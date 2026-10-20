/* switch.S - the x86-64 Windows context switch of the Crug task runtime.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   void crug_ctx_switch (void **save_sp, void *new_sp)

   Microsoft x64 arguments arrive in %rcx and %rdx (not %rdi and %rsi).  The
   ABI makes %rbx, %rbp, %rdi, %rsi, %r12-%r15 and %xmm6-%xmm15 nonvolatile,
   and MXCSR control bits plus the x87 control word must survive a call, so
   every one of them is saved here.  The frame is 240 bytes:

     0..159     xmm6..xmm15 (16 bytes each)
     160        MXCSR (4 bytes), 164 x87 control word (2 bytes)
     168..231   rbx, rbp, rdi, rsi, r12, r13, r14, r15
     232..239   padding
     240        return address of the call to crug_ctx_switch

   After the final ret a fresh task runs with %rsp % 16 == 8, the alignment a
   function entered by a call expects (the Windows x64 rule that call sites
   keep %rsp 16-byte aligned).

   TEB stack limits are not swapped with the context in v1: the runtime keeps
   one stack per task and never grows it (ADR 0008), and no Windows API that
   consults the TEB stack bound is called by the scheduler.  That limitation
   is recorded in docs/crug/PORTABILITY.md.
*/

	.text
	.globl	crug_ctx_switch
	.p2align 4
crug_ctx_switch:
	subq	$240, %rsp
	movdqu	%xmm6, 0(%rsp)
	movdqu	%xmm7, 16(%rsp)
	movdqu	%xmm8, 32(%rsp)
	movdqu	%xmm9, 48(%rsp)
	movdqu	%xmm10, 64(%rsp)
	movdqu	%xmm11, 80(%rsp)
	movdqu	%xmm12, 96(%rsp)
	movdqu	%xmm13, 112(%rsp)
	movdqu	%xmm14, 128(%rsp)
	movdqu	%xmm15, 144(%rsp)
	stmxcsr	160(%rsp)
	fnstcw	164(%rsp)
	movq	%rbx, 168(%rsp)
	movq	%rbp, 176(%rsp)
	movq	%rdi, 184(%rsp)
	movq	%rsi, 192(%rsp)
	movq	%r12, 200(%rsp)
	movq	%r13, 208(%rsp)
	movq	%r14, 216(%rsp)
	movq	%r15, 224(%rsp)
	movq	%rsp, (%rcx)
	movq	%rdx, %rsp
	ldmxcsr	160(%rsp)
	fldcw	164(%rsp)
	movq	168(%rsp), %rbx
	movq	176(%rsp), %rbp
	movq	184(%rsp), %rdi
	movq	192(%rsp), %rsi
	movq	200(%rsp), %r12
	movq	208(%rsp), %r13
	movq	216(%rsp), %r14
	movq	224(%rsp), %r15
	movdqu	0(%rsp), %xmm6
	movdqu	16(%rsp), %xmm7
	movdqu	32(%rsp), %xmm8
	movdqu	48(%rsp), %xmm9
	movdqu	64(%rsp), %xmm10
	movdqu	80(%rsp), %xmm11
	movdqu	96(%rsp), %xmm12
	movdqu	112(%rsp), %xmm13
	movdqu	128(%rsp), %xmm14
	movdqu	144(%rsp), %xmm15
	addq	$240, %rsp
	ret
