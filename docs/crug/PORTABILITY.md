# Crug Portability and the Multi-OS Runtime

Status: **Phase 18 implemented 2026-10-20.** The scheduler core, the channel
runtime and the task runtime build and run on Linux x86-64, Linux AArch64,
Windows x86-64 and macOS ARM64; every backend passes the same PAL contract and
the same `make check-MULTI_OS` command. This page records the matrix, the
mappings and the evidence. The decision record is
[ADR 0012](adr/0012-os-backend-policy.md); the OS boundary itself is
[PAL.md](PAL.md), the architecture side is [TASKS.md](TASKS.md) and ADR 0013.

## 1. Two axes, one core

The runtime is split along two independent axes (MASTER_SPEC section 26.5):

- `libcrug/pal/<os>/pal_<os>.c` - the operating system, implementing
  `<crug_pal.h>`;
- `libcrug/arch/<arch>/<abi>/` - the CPU and calling convention, implementing
  `ctx.h` and the context switch.

`libcrug/core/` includes only those two headers plus plain C headers. It
contains no platform ifdef and no system call; the gate proves that with
`grep -RE '#if.*(_WIN32|__APPLE__|__linux__)' libcrug/core` (zero matches).
A port is a new directory pair, never a branch in the core.

## 2. The v1 matrix

| Host | PAL | Arch | State | Evidence |
|---|---|---|---|---|
| Linux x86-64 | `pal/linux` | `arch/x86_64/sysv` | supported | native `make -C libcrug check-MULTI_OS`; guard-fault test |
| Linux AArch64 | `pal/linux` | `arch/aarch64` | supported | `make -C libcrug check-MULTI_OS TARGET=aarch64-linux` (cross + qemu) and the native `linux-aarch64` CI job |
| Windows x86-64 | `pal/windows` | `arch/x86_64/windows` | supported | MinGW cross-build plus the `windows-x86_64` CI job; wine run recorded in the phase gate |
| macOS ARM64 | `pal/darwin` | `arch/aarch64` | supported | `macos-arm64` CI job (no local macOS host) |
| macOS x86-64 | `pal/darwin` | `arch/x86_64/sysv` | builds, not gated | same sources, Darwin symbol names |
| any host, no OS | `pal/fake` | `arch/x86_64/sysv` | contract backend only | `make -C libcrug check-PAL PAL=fake` |

`pal/fake` is not a product backend: it exists so the contract suite can run
where no POSIX layer exists, and so the suite cannot accidentally depend on one.

## 3. Building and running a port

The build detects the host and every choice stays overridable:

```sh
make -C libcrug check-MULTI_OS                                  # this host
make -C libcrug check-MULTI_OS TARGET=aarch64-linux             # Linux AArch64 cross + qemu
make -C libcrug check-MULTI_OS PAL=fake                         # contract backend
make -C libcrug check-MULTI_OS PAL=windows CC=x86_64-w64-mingw32-gcc
```

- `PAL`, `ARCH`, `CC` and `TARGET` are the four knobs; the Makefile prints
  the detected `PAL`, `ARCH` and compiler in the check output.
- `TARGET=aarch64-linux` selects `CROSS_COMPILE` (default
  `aarch64-linux-gnu-`), builds into `build/linux-aarch64` and runs the
  binaries through `qemu-aarch64` or `qemu-aarch64-static` unless the host is
  already AArch64.
- A Windows cross-build uses `x86_64-w64-mingw32-gcc` and produces
  `libcrug/build/windows/*.exe`; the .exe files run under Windows or wine, not
  on Linux directly.
- The shared check builds the contract, the task suite and three plain-C channel
  programs (buffered round trip, handoff inside a scope, rotating select scan)
  using only `task.h`, `chan.h` and `crug_pal.h`, so the same sources run on
  every host. The Crug-level suites (dg cases, `libcrug/tests/goroutines` etc.)
  need `crugc` and are exercised on Linux.

A local AArch64 run without a native machine uses qemu user emulation; the CI
`linux-aarch64` job runs the same command natively.

## 4. What each backend maps to

| PAL operation | Linux | Windows | Darwin |
|---|---|---|---|
| thread create/join | `pthread_create` / `pthread_join` | `CreateThread` / `WaitForSingleObject` | `pthread_create` / `pthread_join` |
| cpu count | `sysconf(_SC_NPROCESSORS_ONLN)` | `GetSystemInfo` | `sysconf(_SC_NPROCESSORS_ONLN)` |
| wait/wake | `SYS_futex` (private wait/wake), ENOSYS falls back to 1 ms sleeps | `WaitOnAddress` / `WakeByAddressSingle` / `WakeByAddressAll`, resolved by `GetProcAddress`, Sleep polling fallback | hashed table of mutex/condition variable buckets, commit under the bucket lock |
| reserve memory | `mmap(PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS)` | `VirtualAlloc(MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE)` (eager) | `mmap(PROT_NONE, MAP_PRIVATE|MAP_ANON)` |
| commit | `mprotect(PROT_READ|PROT_WRITE)` | committed in reserve; commit keeps the bytes | `mprotect(PROT_READ|PROT_WRITE)` |
| guard page | `mprotect(PROT_NONE)` | `VirtualProtect(PAGE_NOACCESS)` | `mprotect(PROT_NONE)` |
| release | `munmap` | `VirtualFree(MEM_RELEASE)` | `munmap` |
| monotonic clock | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` | `mach_absolute_time` and the mach timebase (nanosecond readings; the timebase ticks at 24 MHz on Apple silicon) |
| sleep | `clock_nanosleep` | `Sleep` | `nanosleep` |

Two consequences worth knowing:

- Windows commits a reservation eagerly, because the guard page of a task stack
  can only be enforced on committed memory; `crug_pal_memory_commit` is
  therefore a no-op that keeps the contract's bytes.
- Darwin has no futex, so wait/wake is a hashed mutex/condition table. The
  predicate stays authoritative, so a spurious wakeup costs a loop turn, and a
  wake of an address that is not waiting is a no-op.

## 5. Context switches

Every switch is arch code under `libcrug/arch/`; the scheduler only passes a
stack pointer. The saved state is exactly the calling convention's
callee-saved set:

| ABI | Saved state | Stack at entry |
|---|---|---|
| SysV AMD64 (`arch/x86_64/sysv`) | rbx, rbp, r12-r15, MXCSR control bits, x87 control word | `%rsp % 16 == 8` |
| Microsoft x64 (`arch/x86_64/windows`) | the SysV set plus rdi, rsi, xmm6-xmm15 | `%rsp % 16 == 8` |
| AAPCS64 (`arch/aarch64`) | x19-x28, x29, x30, low 64 bits of v8-v15 (d8-d15), FPCR | 16-byte aligned |

The two x86-64 ports differ in argument registers (`rdi/rsi/rdx/rcx/r8/r9` versus
`rcx/rdx/r8/r9`) and in the number of nonvolatile registers; both were derived
from the ABI, not from a compiler. TEB stack limits are not swapped on Windows
in v1: the runtime never grows a stack (ADR 0008 D1) and the scheduler calls no
API that consults the TEB bound. No unwinding crosses a switch, and
ThreadSanitizer cannot follow one (ADR 0013 D4, D6).

A switch restores exactly the callee-saved set, which is also why a task is
pinned to the worker that first ran it (ADR 0009 amendment A-1): a compiler may
cache the address of a `_Thread_local` object in a callee-saved register, and
that address is only valid on the thread that produced it. No port may add work
stealing without re-establishing those addresses or forbidding C TLS across
`yield ()`. The Windows and AArch64 ports found this failure with two or more
workers, which is what `scripts/check-multi-os-ports.sh` now exercises at 2, 4
and 8 workers.

## 6. The portability contract

The same sources define every backend:

- `libcrug/core/pal_contract.c` - twelve cases written against
  `<crug_pal.h>` only: version and error spelling, thread create/join, NULL
  entry, CPU count, wait/wake with a predicate, memory reserve/commit/release,
  guard pages (a backend may decline), monotonic clock, sleep, error mapping,
  idempotent shutdown.
- `libcrug/tests/runtime/` - three plain-C channel programs and the task
  suite, which use `task.h` / `chan.h` and the PAL.
- `make check-MULTI_OS` - the one command CI runs on every host; it ends with
  a single line naming the port and the counts, for example
  `multi-os: Linux/x86_64/sysv pal-contract 12 cases, tasks 17 checks, 3 channel program(s)`.

## 7. CI matrix

`.github/workflows/multi-os.yml` has one job per supported host:

| Job | Runner | Command |
|---|---|---|
| `linux-x86_64` | `ubuntu-24.04` | `make check-MULTI_OS` then `make check-MULTI_OS TARGET=aarch64-linux` |
| `linux-aarch64` | `ubuntu-24.04-arm` | `make check-MULTI_OS` |
| `windows-x86_64` | `windows-2022` + MSYS2 MinGW-w64 | `make -C libcrug check-MULTI_OS CC=gcc` |
| `macos-arm64` | `macos-14` | `make -C libcrug check-MULTI_OS` |

The Windows and macOS jobs are meant to be required checks on `main`; that is
an owner action in branch protection. Until it is set, the phase gate records a
green run of the workflow as evidence, and this page names the gap.

## 8. Where platform code may live

| Directory | May contain |
|---|---|
| `libcrug/core/` | no OS API, no platform ifdef, no `asm` (except through `ctx.h`) |
| `libcrug/pal/<os>/` | OS calls, platform ifdefs |
| `libcrug/arch/<arch>/` | assembly, ABI-specific stack preparation |
| `libcrug/tests/` | platform tests (for example the Linux guard-fault child) |

`scripts/lint-no-os-apis.sh libcrug/core` enforces the first row with the gate
list `pthread_|epoll|futex|CreateThread|kqueue|WaitOnAddress` and an extended
list, and the phase gate also requires zero `asm` matches in the core.

## 9. Tests and the phase gate

| Check | What it proves |
|---|---|
| `make check-MULTI_OS` | this host: contract, tasks, channels under one command |
| `make check-MULTI_OS TARGET=aarch64-linux` | Linux AArch64 PAL and AAPCS64 switch |
| `make check-PAL PAL=fake` / `PAL=linux` | backend parity on one contract |
| `make -C libcrug PAL=windows CC=x86_64-w64-mingw32-gcc ...` | the Windows backend and x64 switch compile and link |
| `scripts/lint-no-os-apis.sh libcrug/core` | the core stays OS-free |
| `grep -RE '#if.*(_WIN32|__APPLE__|__linux__)' libcrug/core` | no ifdef sprawl |
| `.github/workflows/multi-os.yml` | all four hosts, the real proof |
| `scripts/gate.sh --run phase-18` | the phase gate over the checks above |

## 10. Limits

- No I/O poller in PAL v1 (deferred to post-1.0); the phase 18 non-goals also
  exclude IOCP, kqueue and io_uring backends.
- No BSD, Windows ARM64 or RISC-V port, and no cross-OS binary compatibility.
- Windows: TEB stack bounds are not swapped; reservations commit eagerly.
- Darwin: wait/wake is a hashed mutex/condition table, not a kernel primitive;
  macOS is proven by CI only.
- The Crug-level dg suites run on Linux; the other hosts prove the runtime, not
  the compiler, which stays a Linux-hosted GCC fork (Phase 18 is a runtime
  portability phase).
- AArch64 support is the runtime and its context switch; the compiler still
  emits for the host GCC's targets, and cross-compiling Crug programs to
  AArch64 is future work.
