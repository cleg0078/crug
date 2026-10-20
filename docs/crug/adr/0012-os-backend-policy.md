# ADR 0012 - OS backend policy: one core, four hosts, no ifdef sprawl

- **Status:** Accepted
- **Date:** 2026-10-20
- **Deciders:** project owner (the v1 platform set), runtime maintainer (the
  backend layout, the Windows and Darwin mappings and the CI matrix)
- **Relates:** ADR 0008 (goroutine stack model), ADR 0009 (scheduler model),
  ADR 0011 (PAL contract), ADR 0013 (arch context switch policy), RFC-0001
  sections 35-36, CRUG_PRODUCTION_ROADMAP section 22, MASTER_SPEC sections 26
  and 79, `docs/crug/PAL.md`, `docs/crug/PORTABILITY.md`,
  `docs/crug/EXIT-GATES.md` phase 18

## Context

MASTER_SPEC section 79 requires this ADR, and section 26.5 makes the operating
system and the CPU/ABI two independent axes. Phase 18 closes M18 by proving that
one scheduler core runs on Linux x86-64, Windows x86-64, macOS ARM64 and Linux
AArch64, and the phase gate forbids platform ifdefs in `libcrug/core`.

After Phases 13-17 the core is already clean (`libcrug/core` names no system
primitive), but the build has only one backend (`pal/linux`) and one context
switch (`arch/x86_64/sysv`); `libcrug/Makefile` hard-codes both. Nothing in
the repository says which platforms v1 supports, what a second backend must
reproduce, how a Windows or Darwin wait/wake is spelled when the OS has no
futex, or how CI proves the matrix instead of one host.

## Decision

### D1 - Two axes, one directory each, and neither axis knows the other

`libcrug/pal/<os>/pal_<os>.c` implements `<crug_pal.h>`;
`libcrug/arch/<arch>/<abi>/` implements `ctx.h` and the switch. The core
includes only the PAL header, the arch `ctx.h` and plain C headers. A port is a
new directory pair, never a branch in the core. `pal/fake` remains the
no-operating-system backend used by the contract's portability proof.

### D2 - The v1 platform set is four hosts

| Host | PAL | Arch directory | Context switch |
|---|---|---|---|
| Linux x86-64 | `pal/linux` | `arch/x86_64/sysv` | SysV AMD64 |
| Linux AArch64 | `pal/linux` | `arch/aarch64` | AAPCS64 |
| Windows x86-64 | `pal/windows` | `arch/x86_64/windows` | Microsoft x64 |
| macOS ARM64 | `pal/darwin` | `arch/aarch64` | AAPCS64 (Darwin symbol names) |

macOS x86-64 also builds (`pal/darwin` + `arch/x86_64/sysv`), but the phase
gate proves the four rows above. BSD, Windows ARM64 and RISC-V are future ports
(roadmap section 22 and MASTER_SPEC section 26.6); nothing in the core changes
to add one.

### D3 - The build detects the host, and every choice stays overridable

`libcrug/Makefile` maps `uname -s`/`uname -m` to a default PAL and arch,
and accepts `PAL=`, `ARCH=`, `CC=` and `TARGET=`. `TARGET=aarch64-linux`
selects the cross compiler (`CROSS_COMPILE`, default `aarch64-linux-gnu-`) and
runs the binaries through `qemu-aarch64` or `qemu-aarch64-static` when the
host is not AArch64. The detected values are printed by `make check-MULTI_OS`,
so a log says which port ran.

### D4 - Every backend passes the same contract, on every platform

`core/pal_contract.c` is written against the header only and is linked
unchanged with each backend. `make check-MULTI_OS` runs that suite, the task
suite and three plain-C channel programs; the command is identical on all four
hosts, and CI runs exactly it. A backend that needs a different contract is not
a backend.

### D5 - Windows mapping

Threads are `CreateThread` and `WaitForSingleObject`; wait/wake is
`WaitOnAddress`, `WakeByAddressSingle` and `WakeByAddressAll`, resolved with
`GetProcAddress` because a MinGW import library may predate Windows 8, with a
Sleep-based polling fallback that keeps the predicate authoritative; memory is
`VirtualAlloc`/`VirtualProtect`/`VirtualFree`; the clock is
`QueryPerformanceCounter`; sleep is `Sleep`.

A Windows reservation commits eagerly: Windows address space is cheap and the
guard page of a task stack can only be enforced on committed memory, so
`crug_pal_memory_reserve` returns ReadWrite memory and `commit` is a no-op
that keeps the contract's bytes.

The Windows x64 context switch saves what the Microsoft ABI makes nonvolatile:
rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15, MXCSR control bits and the x87 control
word. TEB stack limits are **not** swapped with the context in v1; the runtime
never grows a stack (ADR 0008 D1) and the scheduler calls no API that consults
the TEB bound. This is a declared limit, not an accident.

### D6 - Darwin mapping

Threads are POSIX threads; memory is `mmap`/`mprotect`/`munmap`; the clock
is `clock_gettime (CLOCK_MONOTONIC)`; sleep is `nanosleep`. Darwin has no
futex and its ulock is private, so wait/wake is a hashed table of mutex and
condition variable pairs: a waiter rechecks its own predicate under the bucket
lock, and a wake signals or broadcasts the bucket. The predicate stays
authoritative, so a spurious wakeup is harmless; a wake of an address that is
not waiting is a no-op. The kqueue poller is deferred (the phase's non-goals).

### D7 - The context switch is arch code, and the OS only supplies the stack

The scheduler core hands `crug_ctx_switch` a stack pointer; it never sees a
register. Each switch saves exactly the ABI's callee-saved state:

- SysV AMD64: rbx, rbp, r12-r15, MXCSR control bits, x87 control word.
- Microsoft x64: the SysV set plus rdi, rsi, xmm6-xmm15.
- AAPCS64: x19-x28, x29, x30, the low 64 bits of v8-v15 (d8-d15) and FPCR.

The prepared stack leaves the entry function at the alignment the ABI expects
(`%rsp % 16 == 8` on x86-64, 16-byte aligned on AArch64). No unwinding
crosses a switch; debug and sanitizer consequences are recorded in ADR 0013.

### D8 - CI proves the matrix; the local gate proves what a local host can

`.github/workflows/multi-os.yml` has one job per host: `linux-x86_64`
(including `TARGET=aarch64-linux` under qemu), `linux-aarch64` (native ARM
runner), `windows-x86_64` (MSYS2 MinGW-w64) and `macos-arm64`. Every job
runs `make check-MULTI_OS` or the same command with `TARGET`. The Windows
and macOS jobs are required checks in the owner's branch protection; while that
setting is pending, a green run of the workflow is recorded in the phase gate
archive and the progress page names the gap.

### D9 - Limits

No BSD, Windows ARM64 or RISC-V port before 1.0; no IOCP, kqueue or io_uring
backend (post-1.0); no cross-OS binary compatibility; no TEB or Mach thread
state swapping; no dynamic code generation in the scheduler. The Windows and
Darwin backends pass the same contract, not a superset of it.

## Consequences

- A port is a directory plus a Makefile case; the core does not change.
- The contract suite, not documentation, is the definition of a backend.
- Windows commits reserved memory eagerly and does not swap TEB stack bounds;
  both are visible in PORTABILITY.md and in the tests.
- Four CI jobs replace one; a Windows or macOS regression is caught without a
  maintainer owning that OS.

## Alternatives considered

- **One backend with ifdefs in the core.** Rejected: the phase gate forbids it,
  and the PAL exists precisely so the scheduler is testable on a fake OS.
- **Use Darwin's private ulock.** Rejected: private API, and the hashed
  mutex/condition implementation is correct and portable.
- **Save every register on every switch.** Rejected: the ABI already makes the
  rest caller-saved; saving them would slow every switch for no semantics.
- **WaitOnAddress linked directly.** Rejected: MinGW import libraries for older
  Windows versions do not export it; dynamic resolution keeps one binary.
