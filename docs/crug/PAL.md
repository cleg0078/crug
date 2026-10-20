# PAL — the Platform Abstraction Layer

Status: **v1 frozen** (2026-10-19, ADR 0011). The contract is
`libcrug/pal/include/crug_pal.h`; the suite that proves a backend honours it
is `libcrug/core/pal_contract.c`; the Phase 13 gate runs both backends.

```
Phase 13 exists to make one thing true before any scheduler code appears:
libcrug/core cannot reach an operating system except through this header. Every
later port (P18) is then a backend, not a runtime fork.
```

## 1. The boundary

Two axes, never mixed (RFC-0001 section 36):

```text
operating system  ->  libcrug/pal/<os>/
CPU and ABI       ->  libcrug/arch/<arch>/<abi>/
```

`libcrug/core` may include `<crug_pal.h>` and plain C headers. It may not
name a system primitive, not even in a comment, because
`scripts/lint-no-os-apis.sh` matches *names*: a comment that invites the next
reader to call one fails the build too. That is deliberate — the lint is
cheap, and a stale comment is how the boundary erodes.

`libcrug/arch/` holds architecture and ABI dependent context switching. It is
empty in Phase 13; Phase 14 adds the x86-64 System V switch there.

## 2. PAL v1 surface

The header is normative; this table is the map.

| Area | Entry point | Contract |
|---|---|---|
| version | `CRUG_PAL_VERSION_MAJOR`, `CRUG_PAL_VERSION_MINOR`, `CRUG_PAL_ABI_VERSION` | `(1 << 16) | 0` today |
| status | `int crug_pal_thread_create (crug_pal_thread **out, void (*entry)(void *), void *arg)` | 0 on success, non-zero on failure, reason in `crug_pal_last_error()` |
| threads | `void crug_pal_thread_join (crug_pal_thread *thread)` | waits for the entry to finish and releases the handle |
| threads | `uint32_t crug_pal_cpu_count (void)` | at least 1 |
| wait/wake | `void crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected)` | blocks while `*addr == expected`; spurious returns allowed |
| wait/wake | `void crug_pal_wake_one`, `crug_pal_wake_all` | releases one or every waiter on `addr`; a wake with no waiter is not remembered |
| memory | `void *crug_pal_memory_reserve (size_t size)` | inaccessible reservation of at least `size` bytes, or NULL |
| memory | `bool crug_pal_memory_commit (void *ptr, size_t size)` | makes a range readable and writable |
| memory | `bool crug_pal_memory_protect_guard (void *ptr, size_t size)` | makes a range inaccessible; `false` means "not supported", not "error" |
| memory | `void crug_pal_memory_release (void *ptr, size_t size)` | returns a whole reservation; `size` must be the reserve size |
| time | `uint64_t crug_pal_clock_monotonic_ns (void)` | monotonic, nanosecond resolution |
| time | `void crug_pal_sleep_ns (uint64_t ns)` | may over-sleep, must not under-sleep past one tick |
| shutdown | `void crug_pal_shutdown (void)` | releases backend state; idempotent; nothing after it |

Three decisions are worth stating because they are the kind of thing a later
reader will want to change:

- **The predicate is the state, not a wake token.** `wait_u32` takes the
  expected value and loops on its own condition; a lost wake cannot deadlock a
  correct caller.
- **A declined capability is not an error.** Guard pages return `false`. An
  operation the backend genuinely cannot perform reports
  `CRUG_PAL_ENOSYS`, and the core must have a defined fallback.
- **No process-shared wait in v1.** The wake word must live in one address
  space. Cross-process wakeups arrive with shared memory, if ever.

### Consumers

The first client is the scheduler (Phase 14, `docs/crug/TASKS.md`): it starts
its workers with `thread_create`, blocks them in `wait_u32` on the scheduler
epoch and wakes them with `wake_one`/`wake_all`, takes each task stack from
`memory_reserve`/`memory_commit` with a 4 KiB guard page from
`memory_protect_guard`, and reads the clock for its statistics. The contract
suite covers exactly those calls, so a runtime linked against a new backend is
tested by the same twelve cases as the fake one. A task group (Phase 15,
`SCOPE.md`) adds no PAL call of its own: a waiter uses `wait_u32` on the group's
pending count on the main thread and `yield` inside a task. Channels (Phase 16,
`CHANNELS.md`) are clients too: a blocked send or receive yields inside a task
and calls `sleep_ns` on the main thread, and `crug_chan_poll_wait` is the same
pair behind one name. No consumer so far needs a v1 entry point that does not
already exist.

## 3. Error model

| Class | Meaning | Tested by |
|---|---|---|
| `CRUG_PAL_OK` | success; also the state of a clean thread | every case |
| `CRUG_PAL_EINVAL` | a contract precondition is violated (NULL entry, zero-size reservation) | `thread-rejects-null-entry`, `error-mapping` |
| `CRUG_PAL_ENOMEM` | the backend cannot obtain a resource | not triggered deterministically; documented |
| `CRUG_PAL_EAGAIN` | retryable; the caller loops on its predicate | reserved for the poller, not used in v1 |
| `CRUG_PAL_ENOSYS` | the backend does not implement the operation | `wait_u32` fallback path on a kernel without the call |
| `CRUG_PAL_ESYS` | the operating system reported a failure | backend paths |

`crug_pal_last_error()` is per thread and is cleared by the next success, so
a caller may test it immediately after a failure without stale state. Values are
part of the frozen contract: they may be appended to, never renumbered.

## 4. Backends

| | fake (`PAL=fake`) | linux (`PAL=linux`) | windows (`PAL=windows`) | darwin (`PAL=darwin`) |
|---|---|---|---|---|
| threads | records the entry, runs it inside `join` | POSIX threads, one system thread per handle | `CreateThread`, joined with `WaitForSingleObject` | POSIX threads |
| cpu count | 1 | `sysconf(_SC_NPROCESSORS_ONLN)` | `GetSystemInfo` | `sysconf(_SC_NPROCESSORS_ONLN)` |
| wait/wake | records the call, never blocks (the predicate is still inspected) | `SYS_futex` with the private wait/wake operations | `WaitOnAddress`/`WakeByAddress*`, resolved by `GetProcAddress`, Sleep polling fallback | hashed mutex/condition buckets (Darwin has no futex) |
| memory | `malloc`/`free`; commit is a no-op | `mmap` `PROT_NONE`, `mprotect`, `munmap` | `VirtualAlloc` (reserve commits eagerly), `VirtualProtect`, `VirtualFree` | `mmap` `PROT_NONE`, `mprotect`, `munmap` |
| guard pages | declined (`false`) | `mprotect(PROT_NONE)`, proven by a faulting child | `VirtualProtect(PAGE_NOACCESS)` | `mprotect(PROT_NONE)` |
| clock | synthetic, advances 1 µs per reading and by the requested amount on sleep | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` | `clock_gettime(CLOCK_MONOTONIC)` |
| sleep | advances the synthetic clock | `clock_nanosleep(CLOCK_MONOTONIC)`, retried on interruption | `Sleep` | `nanosleep` |
| error slot | thread-local | thread-local | thread-local | thread-local |

The fake backend is a *contract* backend, not a concurrency model: it exists so
the core and the suite can be built where no POSIX layer exists, and so the
suite cannot accidentally depend on one. It must never ship as a production
backend (ADR 0011 D9).

## 5. Contract tests

`make -C libcrug check-PAL PAL=fake` runs `libcrug/core/pal_contract.c` —
twelve cases, all written against the header:

| Case | Proves |
|---|---|
| `version-and-names` | the suite and the backend agree on 1.0 and every error class has a spelling |
| `thread-create-join` | the entry has run before `join` returns, and the handle is not NULL |
| `thread-rejects-null-entry` | a NULL entry fails with `CRUG_PAL_EINVAL` |
| `cpu-count` | the count is plausible (1..65536) |
| `wait-wake` | a waiter that has not observed the wake is released by `wake_all` |
| `wait-returns-when-changed` | a satisfied predicate does not block |
| `memory-reserve-commit-release` | committed bytes survive, including the last one |
| `memory-guard` | a claimed guard page is reported, or the capability is declined |
| `clock-monotonic` | two readings advance and never go backwards |
| `sleep-duration` | a 2 ms sleep does not return early |
| `error-mapping` | a zero-size reservation is `EINVAL` and a success clears the slot |
| `shutdown-idempotent` | two shutdowns are defined behaviour |

Measured on this host (2026-10-19):

```text
pal-contract[fake]:    12 cases, 0 failed, 1 skipped   (memory-guard)
pal-contract[linux]:   12 cases, 0 failed, 0 skipped
pal-contract[windows]: 12 cases, 0 failed, 0 skipped   (wine on Linux, Phase 18)
guard-fault: SIGSEGV on the guarded page as claimed
```

Phase 18 added the Windows and Darwin backends and the AArch64 and Microsoft x64
context switches; every backend links the same `core/pal_contract.c` unchanged
(ADR 0012 D4). `docs/crug/PORTABILITY.md` records the host matrix, the mappings
and the CI jobs.

Two checks make the pair a contract rather than two scripts:

- `scripts/check-pal.sh` compares the case *names* of both runs and fails if
  they differ, if Linux skips anything, if the fake skips anything other than
  guard pages, if the Linux guard page does not fault, or if the fake backend's
  object references a system symbol (`nm -u`). It prints
  `check-pal: fake and linux share 12 contract cases, 0 failures`.
- `tests/linux/pal_guard_fault.c` writes into the guarded range inside a
  child process and requires SIGSEGV; it lives outside `core/` because process
  control is not the core's business.

## 6. Lint

```sh
scripts/lint-no-os-apis.sh libcrug/core
# lint-no-os-apis: 2 file(s) under libcrug/core, 0 operating-system primitive references
```

The gate list is `pthread_|epoll|futex|CreateThread|kqueue|WaitOnAddress`;
the extended list adds `clone|io_uring|VirtualAlloc|mach_|mmap|mprotect|munmap|syscall|gettimeofday|clock_gettime|clock_nanosleep|sysconf|nanosleep|fork`.
Both sets must report zero matches. Architecture code is kept out of the core
by `test "$(grep -R 'asm' libcrug/core | grep -vc '^$' || true)" = 0`.

## 7. Versioning

- **v1 is frozen.** The signatures, the error values and the semantics in this
  document are the contract that the Phase 13 gate adjudicates.
- An additive change (a new entry point, a new error class at the end) bumps
  `CRUG_PAL_VERSION_MINOR` and needs an ADR amendment; existing behaviour may
  not change.
- A breaking change bumps the major version and re-opens M13: every backend must
  pass the suite again, and `ABI.md` records that the runtime contract changed.
- The runtime ABI has its own version (RFC-0001 section 37); the PAL contract
  version is not the runtime ABI version.

## 8. Not in v1

- **No poller.** The roadmap's work item 8 sits behind the phase-13 non-goal
  "no pollers/io_uring (post-1.0, section 32)". A poller interface is the first
  candidate for a v1.1 addition, when an I/O phase needs it; adding it now
  would freeze an interface with no client.
- No FreeBSD or bare-metal backend: Phase 18 added Windows x86-64
  (`pal/windows`) and macOS ARM64 (`pal/darwin`) and proof on Linux AArch64
  (ADR 0012 D2); BSD and bare metal are later ports.
- No task, scheduler, channel or scope API: Phases 14 to 16 added the
  scheduler, task groups and channels as *consumers* (section 2), not as PAL
  entry points, and they remain clients of v1.
- No timers as a service: `sleep_ns` is the only clock-based service, and it
  is not a timer wheel.
- No affinity, priority or naming for threads.

## 9. Tests and gate

| Command | Expectation |
|---|---|
| `make -C libcrug check-PAL PAL=fake` | exit 0; `pal-contract[fake]: 12 cases, 0 failed, 1 skipped` |
| `make -C libcrug check-PAL PAL=linux` | exit 0; `pal-contract[linux]: 12 cases, 0 failed, 0 skipped` and the guard-fault line |
| `make -C libcrug check` | both backends, then the lint |
| `scripts/check-pal.sh` | exit 0; same 12 case names on both backends, no unexpected skip, no system symbols in the fake object |
| `scripts/lint-no-os-apis.sh libcrug/core` | exit 0; 0 references |
| `grep -R 'asm' libcrug/core` | no line |
| `make -C libcrug check-MULTI_OS` | exit 0; contract, tasks and three plain-C channel programs under one command (Phase 18) |
| `make -C libcrug check-MULTI_OS TARGET=aarch64-linux` | exit 0; the Linux AArch64 port and the AAPCS64 switch |
| `make -C libcrug PAL=windows CC=x86_64-w64-mingw32-gcc ...` | the Windows backend and the Microsoft x64 switch cross-build |
| `make check-C_COMPAT` | unchanged: adding libcrug changes no compiler output |

The Phase 13 gate in `EXIT-GATES.md` runs these as checks 1-7; amendment A-16
records the rewrite. The Phase 18 gate adds the multi-OS checks; amendment A-21
records that rewrite.
