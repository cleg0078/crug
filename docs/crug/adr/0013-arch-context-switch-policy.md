# ADR 0013 - Architecture context switch policy: x86-64 SysV first, nothing else in core

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (the x86-64-only MVP), runtime maintainer (the
  register set and the stack contract)
- **Relates:** ADR 0008 (goroutine stack model), ADR 0009 (scheduler model),
  ADR 0011 (PAL contract), MASTER_SPEC sections 25.5 and 26.1, RFC-0001
  sections 6 and 36, CRUG_PRODUCTION_ROADMAP section 19,
  `docs/crug/TASKS.md`

## Context

The M:N scheduler needs to leave one task's C stack and enter another's inside a
single worker thread. GCC offers no portable way to do that, so the switch has
to be a small assembly routine. MASTER_SPEC section 26.1 puts CPU- and
ABI-dependent code under `libcrug/arch/`, and section 79 requires this ADR by
name. RFC-0001 section 36 makes operating system and CPU separate axes: the same
PAL backend may meet several architectures, and the same architecture may be
used by several PAL backends.

The phase-14 non-goals defer Windows and macOS contexts to phase 18, so the
MVP needs one ABI done correctly, not five sketches.

## Decision

### D1 - The switch lives only under `libcrug/arch/x86_64/sysv`

`libcrug/core` includes `ctx.h` and never contains assembly or a register
name; `libcrug/arch/` never includes a system header. The interface is two
functions: `void crug_ctx_switch (void **save_sp, void *new_sp)` and
`void *crug_ctx_prepare_stack (void *stack_top, crug_ctx_entry entry)`. The
core hands over stack pointers and nothing else. On any other architecture
`ctx.h` is a `#error`, so an unsupported port fails at compile time with a
message instead of at run time with a corrupt stack.

### D2 - Save exactly what the SysV ABI calls preserved, plus FP control state

The switch saves `rbx`, `rbp`, `r12`, `r13`, `r14`, `r15` and the return
address, i.e. every callee-saved general register. It does not save the XMM/YMM/
ZMM registers or the x87 data registers, because the ABI makes them
caller-saved: a yield is a call, so the compiler has already spilled anything it
needs across it. It does save the FP **control** state, which the ABI does not:
`MXCSR` (low 16 bits, at offset 0 of one 8-byte slot) and the x87 control word
(offset 4). "Callee-saved" would not save them for us, and a rounding-mode or
denormal-mode change made by one task must not leak into another. A new stack is
prepared with the defaults `CRUG_CTX_MXCSR_DEFAULT` 0x1f80 and
`CRUG_CTX_X87CW_DEFAULT` 0x037f, so a task starts in the state a fresh thread
starts in.

### D3 - Stack alignment is `rsp % 16 == 8` at entry, and the frame is a lie

`crug_ctx_prepare_stack` aligns the top down to 16 and lays out, from the top:
a padding word, the entry address, six zeroed callee-saved slots and the
FP-control slot, so that after `ret` pops the entry address the stack pointer
is 8 bytes below a 16-byte boundary — exactly the state a function sees on
entry under the SysV ABI. The padding word makes the first instruction of the
entry function see a plausible return address; the entry never returns, and
`crug_task_exit` is what happens when it does.

### D4 - No unwinding and no unwinder across a switch

The entry has no caller frame, so unwinding out of a task is undefined: the
switch has `CFI` start/end proc markers so a debugger sees the switch as an
ordinary call, but a backtrace stops at the task entry. `setjmp`/`longjmp`
across a `yield` is outside the contract (ADR 0008 D6). This is the price of
stackful fibers done without compiler support, and it is stated in
`docs/crug/TASKS.md` rather than discovered by a user.

### D5 - Architecture selection is compile-time; other ports are phase 18

The makefiles choose the arch directory from the target triple. There is no
runtime dispatch and no function-pointer indirection in the switch path. A
second ABI (Windows x64, aarch64, riscv64) is a new directory with its own
`ctx.h`/`switch.S` and its own contract test, added when phase 18 opens, and
this ADR is the template for it.

### D6 - ThreadSanitizer cannot follow the switch; ASan and UBSan can

TSan instruments memory accesses and tracks happens-before through pthread
primitives, so a hand-written stack switch looks like a race between the two
task bodies. Task programs are therefore verified with ASan and UBSan
(`make -C libcrug check-sanitizer`), and synchronization is checked by the
scheduler's own stress test (`lost` and `corrupt` counters) rather than by
TSan. This is a known limitation of the MVP, not a claim that tasks are
race-free: phase 17's `Send`/`Sync` analysis is where the race question is
answered statically.

### D7 - Evidence is the stress run, the canary and the sanitizer suite

The switch is accepted as correct when (a) 100000 trivial tasks complete with
`lost=0 corrupt=0` on 4 workers, (b) the canary check reports 0 corruptions,
(c) the whole task suite passes under ASan+UBSan, and (d) `strace` shows 4
thread-creating calls for those 100000 tasks, i.e. the tasks really are not
threads. All four are phase-14 gate checks.

## Consequences

- The switch is about 30 instructions and is auditable in one screen; there is
  no compiler-generated prologue to reason about.
- A task's floating-point control state travels with it and a task cannot
  accidentally change another task's rounding mode.
- Unwinding, TSan and a debugger's cross-task backtrace are stated limits.
- A new architecture is a directory plus a contract test, not a core change.

## Alternatives considered

- **Use `setjmp`/`longjmp` or `ucontext` for the switch.** Rejected: both
  copy a signal mask or a full register set per switch and `ucontext` is
  deprecated on several systems; neither gives control over the ABI's
  caller-saved state.
- **Save the XMM registers too.** Rejected by D2: they are caller-saved, and
  saving 16 or 32 vector registers on every yield would tax the common case to
  protect a case the ABI already forbids.
- **Implement the switch in C with inline asm.** Rejected: the compiler owns
  the frame, so the result depends on optimisation level and the register
  allocator; a standalone assembly file is the only version that can be
  reviewed once and relied on.
- **Runtime dispatch over several arch backends.** Rejected by D5: it costs an
  indirect call in the hottest runtime path and buys a portability the MVP does
  not have.
