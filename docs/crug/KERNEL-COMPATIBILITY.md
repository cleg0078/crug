# CRUG kernel and GNU C compatibility (Phase 2)

**Status:** Phase 2 closed on 2026-10-19 at commit `78c759b`. All four gate checks
pass and are archived (section 7), including a `kernel-compat` check run that
concluded success on that commit and its status as a required check on `main`.

**Closes:** M2 — Linux-kernel/GNU-C baseline, and the evidence basis for conformance
`class K0` (RFC-0001 section 7.2). Phase 2 does **not** claim K0: Phase 19 now
supplies the per-object corpus (`kernel/KERNEL-CORPUS.md`) and the build and boot
evidence (`docs/crug/KERNEL-BASELINE.md` section 8), but this document still
claims only the compile-level baseline.

## 1. What Phase 2 claims

Phase 2 makes GNU C as used by the Linux kernel a measured property of this
compiler instead of an assumption. It adds:

- `-fcrug-kernel`, accepted by the driver and deliberately without effect;
- a named kernel-style corpus at `gcc/testsuite/crug.dg/kernel/`;
- `scripts/test-kernel-compat.sh`, which reports one PASS/FAIL line per work item
  of roadmap section 7 (21 lines in total; the `-nostdlib` item also compiles the
  runtime-free unit) and fails if any one regresses;
- two object-level checks: no Crug runtime symbols, no hidden hosted runtime in a
  freestanding translation unit.

It does **not** benchmark codegen (Phase 21), give
the kernel profile ownership semantics (master-spec stage 8K, optional per roadmap
section 33), or tune any `-O` level.

## 2. `-fcrug-kernel`

The option is declared in `gcc/common.opt` as `Common Ignore`: accepted by the
driver and by every front end, and ignored. That declaration is the whole
implementation:

- the driver accepts it, so command lines written for the kernel profile parse;
- every front end accepts it too. A `Driver`-only declaration was tried first and
  failed: the driver forwards `-f` options to `cc1`, which then reported
  `command-line option '-fcrug-kernel' is valid for the driver but not for C`.
  `Common` is what makes the option legal along the whole pipeline;
- `Ignore` means it sets no variable, so it changes no C semantics, emits no
  predefined macro, and adds no runtime;
- a value form such as `-fcrug-kernel=1` is rejected, because the option takes no
  argument (the runner checks this);
- the objects built with and without the flag are byte-identical (the runner
  checks this too).

Phase 3 gives the flag meaning. Until then a build that passes Phase 2 is the
pre-fork GCC behaviour plus nothing, which is exactly what makes later kernel
regressions attributable.

## 3. The corpus

`gcc/testsuite/crug.dg/kernel/` holds 18 compile-only translation units. They are
checked in place, not generated, so a change to the expected constructs is a
reviewable diff. They include no libc header and call no libc function.

| Source | Roadmap section 7 work item |
|--------|-----------------------------|
| `k01-stmt-expr.c` | statement expressions (item 4) |
| `k02-typeof.c` | `typeof`, `__typeof__` (item 5) |
| `k03-auto-type.c` | `__auto_type` (item 6) |
| `k04-attributes.c` | attributes (item 7) |
| `k05-sections.c` | section placement (item 8) |
| `k06-packed-aligned.c` | packed, aligned (item 9) |
| `k07-zero-length-array.c` | zero-length arrays (item 10) |
| `k08-flexible-array.c` | flexible array members (item 11) |
| `k09-computed-goto.c` | computed goto (item 12) |
| `k10-inline-asm.c` | inline asm (item 13) |
| `k11-asm-goto.c` | `asm goto` (item 14) |
| `k12-volatile-mmio.c` | volatile and MMIO (item 15) |
| `k13-atomics.c` | builtin atomics and barriers (item 16) |
| `k14-branch-prediction.c` | branch prediction hints (item 17) |
| `k15-container-of.c` | `container_of` (item 18) |
| `freestanding.c` | `-ffreestanding` (item 19; the Phase 2 gate path) |
| `nostdlib.c` | `-nostdlib` (item 20) |
| `no-runtime.c` | object-level runtime scan (item 21) |

Corpus rules: kernel style (tabs, no hosted headers, no constructors), one file per
work item, x86_64 as the primary target (AArch64 joins in Phase 8), and every file
must compile at the flags in section 5. Compile-only is deliberate: kernel objects
are linked by the kernel build, not by a user-space driver.

## 4. No-runtime policy

The kernel profile promises no hidden runtime, and Phase 2 checks the object rather
than the source:

- **No `__crug_` symbol**, defined or undefined. A defined symbol would be an
  injected helper; an undefined one would mean the translation unit expects a
  runtime.
- **No `libcrug` dependency** in the dynamic section.
- **No hosted runtime in the freestanding unit**: no `malloc`/`calloc`/`realloc`/
  `free`, no `pthread_*`, no `__cxa_*`, and no `.init_array` section (a constructor
  would run before kernel start-up code is ready).

The checks live in `scripts/check-kernel-objects.sh` and
`scripts/check-kernel-freestanding.sh`. They inspect `nm` and `readelf` output, so
they need host binutils but no part of the compiler under test beyond the objects.

## 5. Running the checks locally

```
make check-KERNEL_COMPAT        # writes out/ and runs all 21 work items
scripts/check-kernel-objects.sh out
scripts/check-kernel-freestanding.sh
make check-kernel-objects       # same object check, through the Makefile
```

`out/` is ignored by git. The runner locates the driver in this order:
`$CRUGC`, `crugc` on `PATH`, then `gcc/crugc`. It exits 2 when no runnable driver
or corpus exists, 1 when any work item fails, and prints
`kernel-compat: 21/21 work items PASS` only on success.

Compile flags, identical for every corpus file:

```
crugc -fcrug-kernel -ffreestanding -nostdlib -O2 -c -o out/<name>.o <source>
```

## 6. Continuous integration and branch protection

The workflow is `.github/workflows/kernel-compat.yml`. It runs one job, named
`kernel-compat`, on `ubuntu-24.04`: it frees disk space, checks the headroom left,
installs the build prerequisites, runs `make baseline JOBS=$(nproc)` (the pinned
GCC 16.2.0 tree is in the repository), then `make check-KERNEL_COMPAT`. It runs on
pushes and pull requests to `main` and on demand, with `contents: read` and a
300-minute cap. It does **not** cache: a build tree is about 5.3 GiB and a stale
cache would silently change what is being tested.

The repository is public since 2026-10-19, so branch protection is free and the job
runs on the public 4 CPU / 16 GB runner. The workflow still frees disk space first:
the image ships toolchains the build never uses, and the GCC build plus the
checkout needs about 8 GiB.

Phase 2 requires the job to be **required** for `main`, not merely green. Branch
protection on `main` requires the status check named exactly `kernel-compat`
(`strict` false, so a later commit does not need a fresh run before landing;
`enforce_admins` false, so the owner can still push directly, while the check binds
every other writer and shows as a red required check on the branch).

The owner-side setup was completed on 2026-10-19:

1. pushes use `GIT_SSH_COMMAND='ssh -F ~/.ssh/config'` because this host's
   `/etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf` has bad ownership;
2. `main` and the tag `releases/gcc-16.2` are pushed to `origin`;
3. Actions is enabled, and the first `kernel-compat` run was queued for the pushed
   commit;
4. branch protection on `main` requires `kernel-compat`;
5. `GH_TOKEN=... scripts/check-ci-gate.sh` prints
   `ci-gate: kernel-compat success=<sha> required=true` when both facts hold.

`scripts/check-ci-gate.sh` reads `GH_TOKEN` or `GITHUB_TOKEN`, resolves
`owner/repo` from `origin` (or `CRUG_REPO`), checks the check run on the exact HEAD
commit, then checks `required_status_checks` for the branch (default `main`, override
with `CRUG_BRANCH`). It exits 2 when tooling or the token is missing, 1 when a fact
does not hold. No token is needed for checks 1-3. A fine-grained token needs
Metadata read plus Contents, Actions and Administration read.

## 7. Exit gate

Roadmap section 7 permits ownership parser work only after this baseline is
CI-protected. The gate is defined in `docs/crug/EXIT-GATES.md`, Phase 2:

| # | Check | Expected |
|---|-------|----------|
| 1 | `bash scripts/test-kernel-compat.sh` | exit 0 across all 21 roadmap work items |
| 2 | `scripts/check-kernel-objects.sh out` | exit 0, no `__crug_`, no `libcrug` |
| 3 | `scripts/check-kernel-freestanding.sh` | exit 0, no hosted runtime, no `.init_array` |
| 4 | `scripts/check-ci-gate.sh` | exit 0, job successful at HEAD and required |

Checks 2 and 3 consume the objects written by check 1, so the gate must run them
in order (it does). Phase 2 is **open** until check 4 passes; local green checks are
evidence for checks 1–3 only, and a declaration that CI is "set up" satisfies
nothing. Check 4 passed at `78c759b` on 2026-10-19.

Recorded runs:

| Date | Commit | Checks 1–3 | Check 4 | Archive |
|------|--------|------------|---------|---------|
| 2026-10-19 | 29868e2 | PASS | FAIL — no `GH_TOKEN` (CI never ran) | `docs/crug/gates/phase-2/29868e2/` |
| 2026-10-19 | 78c759b | PASS | PASS — `kernel-compat` success and required | `docs/crug/gates/phase-2/78c759b/` |

The 29868e2 run recorded only the local half: the CI check failed because no token
existed and the job had never run. The 78c759b run is the closing adjudication;
re-running the gate on a later commit records a new archive, and the old ones stay
as history.

## 8. Known limits

- Compile-only: the corpus never links, so link-time and start-up behaviour of a
  kernel build is not measured here. Phase 19 measures the real kernel build and
  boot in `docs/crug/KERNEL-BASELINE.md` section 8.
- No kernel source in Phase 2: the pin (Linux 6.18.57 LTS) is recorded, and Phase 19
  compiles and boots that tree (`scripts/kernel-stage.sh`, `kernel/KERNEL-CORPUS.md`).
- x86_64 only here. Phase 19 adds a compile-only aarch64 object subset and no
  aarch64 boot.
- The corpus is a floor, not a census: it names the constructs roadmap section 7
  lists, and a construct absent from the list is untested.
- The freestanding unit includes no headers at all; the kernel header probes of
  Phase 19 stage K1 exercise the freestanding header set instead.
- CI is a single job with no matrix and no caching; if it exceeds the 300-minute
  cap the job fails rather than silently truncating.
- Phase 2 cannot detect an optimizer defect that only appears on kernel-shaped
  input at a specific `-O` level; the corpus is fixed and compiled at `-O2`.
