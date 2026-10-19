# libcrug

The Crug runtime library. Phase 13 lands the portability boundary: the PAL
contract (version 1), a contract test suite that lives in the core, and two
backends — a fake one that touches no operating system, and the Linux one.

Nothing else of the runtime exists yet. The scheduler, tasks, scopes and
channels arrive in Phases 14-16 and will be added to this directory; the lint
below is what keeps them from reaching a system call directly.

## Layout

```text
libcrug/
├── core/                      portable code; includes <crug_pal.h> only
│   ├── pal_contract.h
│   └── pal_contract.c         the contract suite (12 cases)
├── pal/
│   ├── include/crug_pal.h     the PAL v1 contract; the only OS boundary
│   ├── fake/pal_fake.c        no OS at all: threads run at join, waits return
│   └── linux/pal_linux.c      POSIX threads, futex, mmap, CLOCK_MONOTONIC
├── arch/                      CPU/ABI context code (Phase 14); empty today
└── tests/
    ├── pal_contract_main.c    runner: one line per case, summary, exit code
    └── linux/pal_guard_fault.c  proves a claimed guard page faults
```

## Build and run

```sh
make -C libcrug check-PAL PAL=fake     # 12 cases, 0 failed, 1 skipped
make -C libcrug check-PAL PAL=linux    # 12 cases, 0 failed, plus guard-fault
make -C libcrug check                  # both, then the portability lint
scripts/check-pal.sh                   # both, compared case by case
```

The skip in the fake run is `memory-guard`: a backend without guard pages
declines the capability by returning false, which the contract allows. The
Linux run has no skips, and `tests/linux/pal_guard_fault.c` proves the claim
by writing into the guarded range in a child process and requiring SIGSEGV.

## Rules

- `core/` includes `<crug_pal.h>` and plain C headers; no system call, no
  system thread or wait primitive, not even in a comment —
  `scripts/lint-no-os-apis.sh` matches names, not calls.
- A backend implements the whole contract or reports `CRUG_PAL_ENOSYS`.
  Declining a capability the header lets it decline is not an error.
- The suite is backend-agnostic: it is written against the header, and
  `scripts/check-pal.sh` fails if the two backends cover different cases.
- `arch/` will hold the context switch; `grep -R 'asm' libcrug/core`
  must stay empty.

The contract itself is specified in `docs/crug/PAL.md` and frozen by
`docs/crug/adr/0024-pal-contract.md`.
