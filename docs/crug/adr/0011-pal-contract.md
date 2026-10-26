# ADR 0011 - PAL contract: one header between the runtime and the OS

- **Status:** Accepted
- **Date:** 2026-10-19
- **Deciders:** project owner (the v1 surface and the "no poller yet" reading of
  the phase-13 non-goals), runtime maintainer (the contract suite and the lint)
- **Relates:** ADR 0006 (C ABI policy: the runtime has its own ABI), ADR 0012
  (OS backend policy, not yet written), ADR 0013 (arch context switch policy, not
  yet written), RFC-0001 sections 35-37, CRUG_PRODUCTION_ROADMAP section 18,
  MASTER_SPEC section 26, `docs/crug/PAL.md`, `docs/crug/EXIT-GATES.md`
  phase 13

## Context

MASTER_SPEC section 79 requires `docs/crug/PAL.md` and an ADR named
`0011-pal-contract`, and section 26.3 sketches a PAL API in eight lines. The
sketch is a list of capabilities, not a contract: it does not say what the error
model is, what a backend may decline, whether the wait predicate is a token or
the state, how a backend proves parity with another backend, or what may live in
`libcrug/core`. RFC-0001 section 35 forbids the core from naming a system
primitive and section 36 splits operating system from CPU; neither fixes the
boundary's shape.

Phase 13's exit gate as written also checks `libcrug/pal/arch/`, while
MASTER_SPEC section 26.1 puts architecture code at `libcrug/arch/` — an
amendment (A-16) corrects the path. And the roadmap's PAL work item lists a
poller while the phase's own non-goals defer pollers to post-1.0; this ADR
records the resolution (D2).

## Decision

### D1 - The header is the only OS boundary, and the boundary is linted

`libcrug/core` includes `<crug_pal.h>` and plain C headers, nothing else.
`libcrug/pal/<os>/` implements the header; `libcrug/arch/<arch>/` holds CPU
and ABI dependent code. The rule is enforced textually
(`scripts/lint-no-os-apis.sh`) against names, including names inside comments:
`pthread_`, `epoll`, `futex`, `CreateThread`, `kqueue`,
`WaitOnAddress` and, in the extended list, `clone`, `io_uring`,
`VirtualAlloc`, `mach_`, `mmap`, `mprotect`, `munmap`, `syscall`,
`gettimeofday`, `clock_gettime`, `clock_nanosleep`, `sysconf`,
`nanosleep`, `fork`. A comment inviting the next reader to call one of them
is how the boundary erodes, so the comment fails too.

### D2 - v1 is threads, CPU count, wait/wake, memory, clock, sleep, errors, shutdown

Those are the capabilities the scheduler and channel phases need, and nothing
more. A **poller is deliberately not in v1**: the phase's non-goal forbids
pollers and io_uring before 1.0 (RFC-0001 section 32), and freezing a poller
interface with no client would be the kind of guess this project's gates exist to
prevent. It is the first candidate for a v1.1 additive change (D8).

### D3 - Capabilities return values; failures return a stable enum in a thread-local slot

Operations that either succeed or fail return `int` (0 = success) and set
`crug_pal_last_error()`; the slot is per thread and cleared by the next
success, so a caller may test it immediately. Error values
(`OK/EINVAL/ENOMEM/EAGAIN/ENOSYS/ESYS`) are frozen: append, never renumber.
Capabilities that a backend may simply not have (guard pages) return `bool`
`false`, which is **declined**, not an error; genuine absence of a required
operation is `CRUG_PAL_ENOSYS` and the core must have a defined fallback.

### D4 - The wait predicate is the state, not a wake token

`crug_pal_wait_u32 (addr, expected)` blocks while `*addr == expected` and
allows spurious returns; a wake with no waiter is not remembered. This makes a
lost wake a performance problem rather than a deadlock, and it lets the Linux
backend use the private wait operation without a second lock. v1 has no
process-shared wait; the word must live in one address space.

### D5 - The contract suite lives in core and is backend-agnostic

`libcrug/core/pal_contract.c` includes only `<crug_pal.h>` and language
headers (no `stdio`; it reports through a callback), so it compiles wherever a
backend does. Twelve cases cover create/join, invalid arguments, CPU count,
wait/wake, the satisfied-predicate path, reserve/commit/release, guard pages,
monotonic time, sleep duration, error mapping and idempotent shutdown.
`scripts/check-pal.sh` enforces **parity**: the two backends must report the
same case names, Linux must skip nothing, the fake backend may skip only guard
pages, and the fake object must reference no system symbol.

### D6 - The fake backend is a contract backend, never a shipping backend

It runs a thread's entry inside `join`, never blocks on a wait, backs memory
with `malloc`, declines guard pages, and advances a synthetic clock by 1 µs
per reading and by the requested amount on sleep. That gives the suite a defined
answer on a machine with no POSIX layer and proves the core does not depend on
one. Shipping it as the production backend is a stop condition (D9).

### D7 - Guard pages are proven by a fault, outside core

`crug_pal_memory_protect_guard` returns `bool`; on Linux it maps and then
`mprotect`s a range `PROT_NONE`, and
`libcrug/tests/linux/pal_guard_fault.c` forks, writes the guarded byte and
requires SIGSEGV. The proof needs process control, which must not appear in
`core/`, so it lives in the test directory and runs only for `PAL=linux`.

### D8 - Versioning is additive-or-breaking, with no in-between

Additive changes (a new entry point, a new error class appended) bump
`CRUG_PAL_VERSION_MINOR` and require an ADR amendment; existing behaviour may
not change. A breaking change bumps the major version and re-opens M13: every
backend passes the suite again and `ABI.md` records that the runtime contract
changed. The PAL version is not the runtime ABI version (RFC-0001 section 37).

### D9 - Stop conditions

- A system primitive name appearing in `libcrug/core` (code or comment): P0.
- A backend-specific detail in `crug_pal.h` (a type from `pthread.h`,
  `HANDLE`, an `epoll_event`): P0.
- Two backends running different case sets, or a skip that is not the documented
  guard-page decline: P0.
- The fake backend shipped as the production backend: P0.
- Starting a second OS port before Phase 18: out of scope, not a defect.

## Consequences

- Every later phase (scheduler, tasks, channels, timers) is written against
  twelve functions and one error enum, so a port is a backend and not a fork.
- The cost is a small indirection on every runtime call: thread creation is one
  call deeper, waits go through one function. Both are outside measured hot
  loops; the phase-11 bench matrix does not include the runtime.
- The lint is textual and therefore occasionally strict about prose. That is
  cheaper than a linker check, and it fails loudly rather than silently.
- The guard-page capability is proven on Linux only until another port exists;
  the suite reports it as a skip elsewhere, which is visible in the log rather
  than hidden.

## Alternatives considered

- **Put the PAL under `libcrug/pal/arch/` as the old gate text said.** Rejected:
  MASTER_SPEC section 26.1 separates OS and CPU axes, and mixing them is how a
  port becomes a rewrite (RFC-0001 section 36).
- **Include the poller now.** Rejected by D2: no client exists before the I/O
  phase, and an interface with no caller cannot be validated.
- **Make every operation return `crug_pal_error` directly.** Rejected: the
  Boolean capabilities (commit, guard) read better as `bool`, and a uniform
  enum would force `OK`/`false` ambiguity into the header.
- **Use a wake counter instead of a state predicate.** Rejected by D4: a lost
  wake would lose the event.
- **Put the contract suite in `libcrug/tests/`.** Rejected: the suite is part of
  what "the core compiles against the fake PAL" means, and the gate says the
  core compiles against the fake PAL.

## Amendments

### A-1 (2026-10-20): v1.1 adds `crug_pal_page_size`

Phase 18 ported the runtime to macOS ARM64, where the page size is 16 KiB. The
scheduler had placed the guard at a fixed 4096 bytes, so `reserve + 4096` was
not page-aligned and `mprotect` returned `EINVAL`; every task spawn aborted
with "cannot commit a task stack". A backend cannot promise aligned memory
operations without exposing its granularity, so v1.1 adds one query:

```c
size_t crug_pal_page_size (void);
```

It returns the granularity of `memory_reserve`, `memory_commit` and
`memory_protect_guard`: a power of two, at least 4096. The fake backend returns
4096, Linux and Darwin `sysconf (_SC_PAGESIZE)` (with a 4096 fallback), and
Windows `GetSystemInfo`'s `dwPageSize`. `CRUG_PAL_VERSION_MINOR` is 1 and
`CRUG_PAL_ABI_VERSION` is `(1 << 16) | 1`; D8's additive rule is what this
amendment uses, and no existing signature or behaviour changed.

The suite still has twelve cases: `memory-reserve-commit-release` now asserts
that the page size is a power of two of at least 4096 and reserves
`page * 256` bytes, so the contract is tested rather than described. The
Consequences above read thirteen functions from v1.1 on.

## Amendment A-2 (2026-10-26): `crug_pal_thread_join` returns `int`

D8's additive rule was used for v1.1; v1.2 is the one recorded exception before
1.0. `crug_pal_thread_join` changes from `void` to `int`: 0 means the thread
was joined, and a nonzero value means the join failed, in which case the
backend must not free the handle and must not report the thread as joined.
There is no retry contract; the caller decides. The four backends
(`linux`, `windows`, `darwin`, `fake`) return the native result
(`pthread_join`, `WaitForSingleObject`/`CloseHandle` status), and the fake
backend honours `CRUG_FAKE_JOIN_FAIL` and returns 1 without freeing. A NULL
handle returns 1. `CRUG_PAL_VERSION_MINOR` is 2 and `CRUG_PAL_ABI_VERSION` is
`(1 << 16) | 2`; the header is 1.2.

The suite has thirteen cases: `thread-create-join` asserts the 0 result, and
`thread-join-failure` asserts that a failing join returns nonzero, keeps the
handle and leaves `joined` clear. The existing twelve cases and every other
signature are unchanged. From 1.0 on, a signature change of this kind is a new
major version; the exception is recorded here because at rc1 the PAL has no
consumer outside this repository and all four backends changed in the same
commit. The Crug runtime ABI stays 1 (ABI.md section 6).
