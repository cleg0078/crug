# CRUG

C + Rust + Go = CRUG. An extension of GCC that brings Rust-style memory
management and Go-style concurrency to C code, with C ABI compatibility as a hard
requirement.

## Status

Early implementation. The repository contains specification documents,
architecture decisions, and the pinned GCC 16.2.0 tree that the fork modifies
(the repository root is the GCC top level; the compiler is `gcc/`; the import
is tagged `releases/gcc-16.2`).

### Phase 1 - Plain C Compatibility Mode

Phase 1 of the release gates is implemented: `gcc/crugc` is the CRUG driver,
which for now behaves exactly like the GCC it is built from, so plain C
compiles and links identically.

### Phase 2 - Linux Kernel / GNU C Compatibility Baseline

Phase 2 (kernel/GNU C compatibility) is closed: the driver accepts
`-fcrug-kernel` as a mode marker, the kernel corpus and its no-runtime checks
live in `gcc/testsuite/crug.dg/kernel/`, and the `kernel-compat` CI job builds
the pinned baseline on every push and is a required status check on `main`.

### Phase 3 - Feature Flags and Contextual Syntax Infrastructure

Phase 3 (feature flags) is closed: ten `-fcrug-*` options, their closure and
the feature macros - see [docs/crug/FLAGS.md](docs/crug/FLAGS.md).

### Phase 4 - Ownership MVP (`own` + `move`)

Phase 4 (ownership MVP) implements `own` and `move`: qualifiers are erased from
the type, `move` lowers to a register copy, use-after-move and implicit copy
are compile errors, and `-fcrug-dump=ownership` prints the post-join state -
see [docs/crug/OWNERSHIP.md](docs/crug/OWNERSHIP.md).

### Phase 5 - Automatic Drop / RAII

Phase 5 (automatic destruction) implements `adopt`, `release`, `drop` and drop
insertion on every exit path, with `setjmp` and never-adopted values diagnosed
- see [docs/crug/DROP.md](docs/crug/DROP.md).

### Phase 6 - Lexical Borrow Checker (`ref` / `mut`)

Phase 6 (lexical borrow checker) implements the `ref` and `mut` qualifiers and
the `borrow`/`borrow_mut` intrinsics: many shared borrows or exactly one
mutable borrow, a lexical lifetime, and compile errors for every conflicting
use - see [docs/crug/BORROW.md](docs/crug/BORROW.md).

### Phase 7 - CFG Borrowing and Non-Lexical Lifetimes

Phase 7 (non-lexical lifetimes) makes a borrow end at its last use and fixes
the interprocedural policy at depth zero: a borrow may cross a call only as a
`ref`/`mut` argument and may not be returned or stored - see
[docs/crug/BORROW.md](docs/crug/BORROW.md) and ADRs
[0019](docs/crug/adr/0019-interprocedural-borrow-policy.md) and
[0020](docs/crug/adr/0020-last-use-liveness.md).

### Phase 8 - Strict Mode, `raw`, and `unsafe`

Phase 8 (strict mode) adds `raw` pointers and `unsafe` blocks: `-fcrug-strict`
rejects unanalysed operations outside an `unsafe` block, with stable
`crug-SAFE-*` codes, and never changes generated code - see
[docs/crug/STRICT.md](docs/crug/STRICT.md) and
[ADR 0021](docs/crug/adr/0021-strict-mode-policy.md).

### Phase 9 - Production C FFI Layer

Phase 9 (C FFI contracts) reads `.crugffi` sidecars: an annotated library hands
out owned values with a known destructor, consuming functions require `move`,
and `-fcrug-ffi-discover` finds a sidecar next to a quoted include. Metadata
never changes codegen - see [docs/crug/FFI.md](docs/crug/FFI.md) and
[ADR 0007](docs/crug/adr/0007-ffi-metadata-format.md).

### Phase 10 - Safe Core Types

Phase 10 adds the safe core types: `slice<T>` with checked `s[i]` indexing,
`option<T>`, `result<T,E>`, payload enums, their value constructors and a
`match` statement over all three sum shapes. They are ordinary C aggregates
with frozen layouts, no runtime and no helper calls - see
[docs/crug/SAFE-TYPES.md](docs/crug/SAFE-TYPES.md) and the frozen rows of
[docs/crug/ABI.md](docs/crug/ABI.md). A payload may be owned (`own char *`): it
is transferred into the constructor, dropped only when its variant is active,
and moved into `match` (`crug-ADT-015`/`-018`/`-019`/`-020`). The Phase 10 gate
passes 10/10 at `43996fe` (logs under `docs/crug/gates/phase-10/43996fe/`).

### Phase 11 - Ownership-Aware Optimization

Phase 11 makes ownership visible to the optimizer: with the opt-in
`-fcrug-alias-facts`, a `mut` parameter (an exclusive borrow) and an `own`
parameter (the only owner) are compiled as `restrict`, and the caller must pass
`borrow_mut`/`borrow` - a plain pointer at a `mut` parameter is rejected
(`crug-BORROW-021`), as are two views of one lender in a single call. The phase
rests on byte identity: the Crug kernel objects of the `bench/` matrix are
identical to their hand-written `restrict` twins at `-O2`/`-O3`, so the
optimizer receives exactly the fact the source proves and nothing more - see
[docs/crug/OPTIMIZATION.md](docs/crug/OPTIMIZATION.md) and
[ADR 0010](docs/crug/adr/0010-noalias-lowering.md). The Phase 11 gate passes
7/7 at `135dea9` (logs under `docs/crug/gates/phase-11/135dea9/`).

### Phase 12 - `c2crug` Migration Tool Integration

Phase 12 adds `c2crug`, a migration tool: it inserts `own` only where a local
structural proof shows exactly one release and no escape, rewrites that release
to `drop`, keeps every other line byte-identical and reports the case as `raw`;
the phase rests on a corpus with its own green test suite (`migration/`) and a
machine-readable report - see [docs/crug/C2CRUG.md](docs/crug/C2CRUG.md) and
[ADR 0023](docs/crug/adr/0023-c2crug-migration.md). The Phase 12 gate passes
7/7 at `3aa78e7` (logs under `docs/crug/gates/phase-12/3aa78e7/`).

### Phase 13 - Platform Abstraction Layer (PAL)

Phase 13 adds `libcrug`'s platform abstraction layer: one header
(`libcrug/pal/include/crug_pal.h`) is the only way the runtime may touch an
operating system, two backends implement it (a fake one with no OS at all and a
Linux one on `pthread`/`futex`/`mmap`), and a twelve-case contract suite runs
against both so a port is a backend and not a fork - see
[docs/crug/PAL.md](docs/crug/PAL.md) and
[ADR 0011](docs/crug/adr/0011-pal-contract.md). The Phase 13 gate passes 7/7 at
`e8a0b22` (logs under `docs/crug/gates/phase-13/e8a0b22/`).

### Phase 14 - Goroutine / Task Runtime MVP

Phase 14 adds the goroutine runtime: `go f (args);` lowers to a per-call-site
frame struct, a static thunk and one `crug_task_spawn` call, M tasks run on N
worker threads through the PAL, each task has a fixed stack with a guard page,
and the capture rules (CONC-008, CONC-010 to CONC-014) make ownership explicit
at the task boundary - see [docs/crug/TASKS.md](docs/crug/TASKS.md),
[docs/crug/GOROUTINES.md](docs/crug/GOROUTINES.md),
[ADR 0008](docs/crug/adr/0008-goroutine-stack-model.md),
[ADR 0009](docs/crug/adr/0009-scheduler-model.md) and
[ADR 0013](docs/crug/adr/0013-arch-context-switch-policy.md). The Phase 14 gate
passes 8/8 at `2ecfe35` (logs under `docs/crug/gates/phase-14/2ecfe35/`).

### Phase 15 - Structured Concurrency

Phase 15 adds structured concurrency: `scope { ... }` is a task group, every
`go` inside the block joins it, and the block waits for every child before it
ends - on the normal path, on `return`, `break`, `continue` and `goto` out. A
scoped task may borrow a value that outlives the block (a mutable borrow must
still be exclusive), a `goto` into a scope body is rejected (`CONC-015`) and so
is a borrow of a value that dies before the scope (`CONC-016`) - see
[docs/crug/SCOPE.md](docs/crug/SCOPE.md) and
[ADR 0024](docs/crug/adr/0024-structured-scope.md). The Phase 15 gate passes
7/7 at `1f42cc7` (logs under `docs/crug/gates/phase-15/1f42cc7/`).

### Phase 16 - Channels and `select`

Phase 16 adds channels and `select`: `chan T` is one opaque pointer whatever
the element type is, `channel (n)` takes its type from the context, capacity 0
is a handoff, and a `select` arm is `case recv (ch)`, `case v = recv (ch)`,
`case send (ch, expr)` or `default` in any order with the default last. A send
of an owning element requires `move` (the receiver destroys it), a destructor
cannot cross a channel, and a `select` arm cannot bind a receive from an
owning-element channel - see [docs/crug/CHANNELS.md](docs/crug/CHANNELS.md) and
[ADR 0025](docs/crug/adr/0025-channels-and-select.md). The Phase 16 gate passes
7/7 at `601e0cc` (logs under `docs/crug/gates/phase-16/601e0cc/`).

### Phase 17 - Send/Sync-Like Static Concurrency Safety

Phase 17 adds the static crossing rules: a captured or sent value must be
`Send` (a raw or plain pointer and anything containing one is not), a shared
borrow captured by a scoped task must be `Sync`, and a thread-affine FFI handle
cannot leave its thread - see [docs/crug/SEND-SYNC.md](docs/crug/SEND-SYNC.md)
and [ADR 0026](docs/crug/adr/0026-send-sync.md). The checks ride on the
goroutine and channel flags, report `CONC-024` to `CONC-027`, and the only
widening is a `.crugffi` `send sync` annotation. The Phase 17 gate passes 7/7
at `42669c5` (logs under `docs/crug/gates/phase-17/42669c5/`).

### Phase 18 - Multi-OS Runtime

Phase 18 makes the runtime portable: the same scheduler core, channel runtime
and task runtime build and run on Linux x86-64, Linux AArch64, Windows x86-64
and macOS ARM64, because the OS and the CPU are two separate directories
(`libcrug/pal/<os>` and `libcrug/arch/<arch>/<abi>`) and the core names no OS
primitive and no platform ifdef. Every backend links the same twelve-case PAL
contract and the same `make check-MULTI_OS` command (contract, task suite and
three plain-C channel programs) runs on every host; Windows and Darwin have
their own wait/wake mappings and the Microsoft x64 and AAPCS64 context switches
save their own callee-saved sets - see
[docs/crug/PORTABILITY.md](docs/crug/PORTABILITY.md) and
[ADR 0012](docs/crug/adr/0012-os-backend-policy.md).

### Phase 19 - Linux Kernel Build Qualification

Phase 19 builds and boots the real kernel: the pinned Linux 6.18.57 tree
compiles with `CC=gcc/crugc` with 0 errors and 0 warnings (2,980 translation
units), produces `vmlinux`, `bzImage` and `System.map`, and boots under QEMU to
a freestanding `/init` that prints `crug-boot-ok` once and powers off. The
evidence is staged: eight header probes, twelve translation units, `lib/` as a
whole, the 438-object pinned corpus, the full build, the boot, and fifteen
aarch64 objects compiled with a cross compiler built from this same tree
(`kernel/KERNEL-CORPUS.md`, `scripts/kernel-stage.sh`). The object diff
rebuilds the same corpus with the wrapped GCC driver and finds every object
byte-identical except the one that embeds the build timestamp
(`kernel/object-diff-allow.txt` records the reason). Kernel code stays plain
GNU C: no ownership lowering, no drop insertion, no runtime symbol, as recorded
in [ADR 0017](docs/crug/adr/0017-kernel-ownership-policy.md) (proposed).

### Phase 20 - Diagnostics, Tooling, Developer Experience

Phase 20 makes the diagnostics usable off the console: every diagnostic that
carries a stable code carries it as a GCC diagnostic rule, so text output ends
in `[crug-<AREA>-<NNN>]` and SARIF (the only machine-readable format GCC 16.2
has; there is no JSON) puts that code in `ruleId` with a `helpUri` that points
at [docs/crug/DIAGNOSTICS.md](docs/crug/DIAGNOSTICS.md). The fifty most common
error classes have one fixture and one golden each under
`gcc/testsuite/crug.dg/diagnostics/`, so a wording drift fails
`make check-DIAGNOSTICS`; the classes that can point at an earlier construct
say where (`note: value was moved here`, `note: borrow created here`).
[TOOLING.md](docs/crug/TOOLING.md) records how editors, static analysers and
debuggers are meant to consume the compiler,
[MIGRATION-EXAMPLES.md](docs/crug/MIGRATION-EXAMPLES.md) shows the before and
after of a migration with executable examples, and
[ADR 0027](docs/crug/adr/0027-tooling-decisions.md) (proposed) fixes the
decisions behind them.

### Phase 21 - Performance Qualification

Phase 21 measures the zero-cost claim instead of asserting it:
`bench/run.py --zero-cost --check` compiles four Crug constructs (an owned
allocation with automatic drop, `move`, and `ref` and `mut` parameters) against
their plain-C twins at -O0/-O2/-O3 and finds the optimised tree dump and the
instruction stream identical in all twelve comparisons (0 added instructions, 0
added calls). `--pristine --check` compiles the corpus and the benchmark
kernels with an unmodified GCC 16.2 built by `make pristine-xgcc` and gets
byte-identical objects (114 of 114 pairs), which is the only non-tautological
plain-C comparison in the project. `--plain-c --check` re-checks the recorded
baseline (object identity enforced). `--concurrency --check` spawns 200 tasks
(Crug/pthread dispatch ratio 0.82; 67.7 KiB of memory per task against 8032.1
KiB for a pthread) and runs the channel arms (197 ns per round trip, 42.5M
elements/s, 4224 B per channel). `scripts/check-kernel-perf.sh` compares the
438-object kernel corpus: 436 objects byte-identical, the two explained objects
in `kernel/object-diff-allow.txt` (one records the build timestamp, the other
the compiler identification the kernel copies from `$CC --version`), and the
disassembly, `.text` sizes and symbol counts identical for all 438. The
thresholds are ratified in
[docs/crug/RELEASE-POLICY.md](docs/crug/RELEASE-POLICY.md) and the measured
results and method are in [docs/crug/PERFORMANCE.md](docs/crug/PERFORMANCE.md);
`make check-PERFORMANCE` runs all five checks and writes the report.

### Phase 22 - Security and Reliability Qualification

Phase 22 hardens what the earlier phases built. `make check-FUZZ` runs six
harnesses: four compiler targets (parser, ownership, borrow and `.crugffi`,
1,200 mutated inputs at the default budget) and two runtime harnesses (2,000
scheduler sequences and 20,000 channel runs), all with zero crashes, zero
timeouts and zero new findings. `make check-SANITIZER` runs ASan, UBSan and
LeakSanitizer over the task, scope and channel suites and a ThreadSanitizer arm
over an OS-thread channel exchange; the fiber-switch exception is
[ADR 0028](docs/crug/adr/0028-tsan-fiber-exception.md) (proposed).
`scripts/check-p0-open.sh` and `scripts/check-regressions.sh` enforce the
defect ledger: no open P0, and every fixed P0/P1 names an existing regression
path. `.github/workflows/fuzz-nightly.yml` runs the same checks nightly at the
larger budget, and `scripts/check-fuzz-nightly.sh` counts 30 consecutive
successful scheduled runs; that window is calendar time, so the Phase 22 gate
stays honest about whether it has been met. The harnesses already earned their
keep: the compiler target found K-002, an internal compiler error when a parse
error left `error_mark_node` as a function body, now fixed and pinned by
`gcc/testsuite/crug.dg/ownership/error-mark-body.c`. The budgets and thresholds
are ratified in [docs/crug/RELEASE-POLICY.md](docs/crug/RELEASE-POLICY.md)
section 3 and the operator's guide is
[docs/crug/FUZZING.md](docs/crug/FUZZING.md).

### Phase 23 - Documentation and Compatibility Policy

Phase 23 freezes the documentation set. The manifest
[docs/crug/REQUIRED-DOC-SECTIONS.md](docs/crug/REQUIRED-DOC-SECTIONS.md) names
the 44 documents and the sections each one must carry, and `make check-DOCS`
(run by `scripts/check-docs.sh`) fails on a missing document, a missing title
or `Status:` line, a missing section, or a paragraph that presents a roadmap
§32 post-1.0 feature as present. The eleven pages that were missing are
written: the language reference ([LANGUAGE.md](docs/crug/LANGUAGE.md),
[GRAMMAR.md](docs/crug/GRAMMAR.md), [BORROWING.md](docs/crug/BORROWING.md),
[UNSAFE.md](docs/crug/UNSAFE.md)), the boundary and runtime references
([C-FFI.md](docs/crug/C-FFI.md), [CONCURRENCY.md](docs/crug/CONCURRENCY.md),
[FREESTANDING.md](docs/crug/FREESTANDING.md)) and the practitioner pages
([TESTING.md](docs/crug/TESTING.md),
[COMPILER-INTERNALS.md](docs/crug/COMPILER-INTERNALS.md),
[MIGRATION.md](docs/crug/MIGRATION.md)).
[docs/crug/COMPATIBILITY-POLICY.md](docs/crug/COMPATIBILITY-POLICY.md) states
the promises the set implies - language, ABI, `.crugffi` and libcrug
versioning, deprecation, the GCC baseline, platforms, C dialects and the kernel
policy - and each item points at the phase or ADR that owns its number.

### Phase 24 - Packaging and Distribution

Phase 24 packages what the earlier phases built. `VERSION` holds `0.1.0`, and
`gcc/crugc --version` now reports the compiler, language, Crug ABI, runtime
ABI, FFI metadata and PAL versions on separate lines (RFC 0001 section 8.2).
`scripts/package.sh` builds a release from the pinned tree: it installs the
compiler with `DESTDIR`, adds the Crug headers, `lib/libcrug.a` and the licence
texts, prunes the C++ front end, strips the binaries, and writes a
deterministic tarball (`crug-0.1.0-linux-x86_64.tar.gz`, 142,961,854 bytes, 971
files), an SPDX 2.3 SBOM and `SHA256SUMS` into `dist/`;
`scripts/check-release.sh` verifies the versions, checksums, SBOM and tarball
shape. `packaging/Dockerfile` builds the multi-stage image `crug/pkg:0.1.0`
from the same tree (builder: the baseline plus
`scripts/package.sh --stage /stage`; runtime: `binutils`, `libc6-dev`,
`libzstd1` and `/opt/crug`), and the three smoke programs in `packaging/smoke/`
(plain C, ownership with `move`, a `go`/`chan` runtime sample) must build and
run inside it. `make package`, `make package-image` and `make check-PACKAGING`
run the flow; the build and install guide is
[docs/crug/BUILD-INSTRUCTIONS.md](docs/crug/BUILD-INSTRUCTIONS.md) and the
version promises are
[docs/crug/COMPATIBILITY-POLICY.md](docs/crug/COMPATIBILITY-POLICY.md). Windows
x86-64 and macOS ARM64 binary packages are not built yet (no packaging host in
this environment); they are recorded as open in
[docs/crug/progress.md](docs/crug/progress.md).

### Phase 25 - Release Candidate

Phase 25 assembles the release candidate `v0.1.0-rc1` and does not pretend the
milestone is closed. The candidate pages are
[docs/crug/RELEASE-NOTES.md](docs/crug/RELEASE-NOTES.md) (what the candidate
is, the qualification evidence, install and verify, the known limitations) and
[CHANGELOG.md](CHANGELOG.md); `make check-RC` runs the six Phase 25 checks, and
`scripts/check-gate-archives.sh` replays every archived gate log the way
`scripts/gate.sh` adjudicates it, so an archive cannot claim a pass its log
does not show. Two of the six checks cannot pass today, and the repository says
so: `scripts/check-fuzz-nightly.sh --window 30` reports the M22 window at 0 of
30 scheduled successes because the first scheduled run failed and reset it, so
the phase-22 archive is not green either, and `docs/crug/progress.md` records
both. M23 stays open until the window fills; no gate result claims otherwise.

See [docs/crug/progress.md](docs/crug/progress.md) for what is decided, what is
open, and what to do next.

## Reading order

1. [spec/000-PRECEDENCE.md](spec/000-PRECEDENCE.md) - which document wins when
   they disagree, plus the conflict register.
2. [spec/CRUG_RFC_0001.md](spec/CRUG_RFC_0001.md) - conformance classes, invariants,
   ABI and zero-cost promises.
3. [spec/CRUG_MASTER_SPEC_EN.md](spec/CRUG_MASTER_SPEC_EN.md) - syntax, semantics,
   compiler architecture.
4. [spec/CRUG_RUST_MEMORY_GAPS.md](spec/CRUG_RUST_MEMORY_GAPS.md) - the detailed
   ownership model that must be merged into the normative text.
5. [spec/CRUG_PRODUCTION_ROADMAP.md](spec/CRUG_PRODUCTION_ROADMAP.md) - phases,
   gates, milestones.

Architecture decisions live in [docs/crug/adr/](docs/crug/adr/); release gates in
[docs/crug/EXIT-GATES.md](docs/crug/EXIT-GATES.md). The C ABI policy is
[docs/crug/adr/0006-c-abi-policy.md](docs/crug/adr/0006-c-abi-policy.md) and the ABI facts it governs are
registered in [docs/crug/ABI.md](docs/crug/ABI.md). The safe core types are
digested in [docs/crug/SAFE-TYPES.md](docs/crug/SAFE-TYPES.md) (what parses per
flag, the frozen layouts, the bounds check, and what is still open). The C-to-Crug
migration tool is specified in [docs/crug/C2CRUG.md](docs/crug/C2CRUG.md) (proof
rules, modes, confidence, report, corpus). The runtime's operating-system
boundary is specified in [docs/crug/PAL.md](docs/crug/PAL.md) (contract, error
model, backends, suite). The task runtime and its stack and scheduler
decisions are in [docs/crug/TASKS.md](docs/crug/TASKS.md), and the `go` surface
and its lowering in [docs/crug/GOROUTINES.md](docs/crug/GOROUTINES.md); the
structured `scope` block is in [docs/crug/SCOPE.md](docs/crug/SCOPE.md) and
channels and `select` in [docs/crug/CHANNELS.md](docs/crug/CHANNELS.md). The C FFI
metadata format is
[docs/crug/adr/0007-ffi-metadata-format.md](docs/crug/adr/0007-ffi-metadata-format.md)
(`.crugffi` v1, with a reference fixture at
`gcc/testsuite/crug.dg/ffi/libc.crugffi`). Diagnostic codes are registered
in [docs/crug/DIAGNOSTICS.md](docs/crug/DIAGNOSTICS.md), and
`scripts/check-diagnostics.sh` keeps the registry consistent with the docs.
Moving the fork to a newer GCC is measured rather than guessed:
[docs/crug/GCC-UPGRADE.md](docs/crug/GCC-UPGRADE.md) records the conflict surface
(9 upstream files, 32 hunks), the porting map, the behavioural pins, the upgrade
procedure and a 31-check acceptance matrix, governed by
[ADR 0022](docs/crug/adr/0022-gcc-baseline-upgrade-policy.md).

## What Crug adds

- `own`, `ref`, `mut`, `raw` qualifiers with move-only ownership and
  deterministic destruction, without changing C layout or calling conventions.
- Borrow checking (N shared or one exclusive) enforced at compile time until the
  borrow's last use (Phases 6-7), with borrows that escape a call rejected and
  `ref`/`mut` parameters as the only borrow points at a call (ADR 0019).
- `raw` pointers and `unsafe` blocks with strict mode: an unanalysed operation
  is an error unless the programmer asks for it explicitly (Phase 8).
- C FFI contracts: a library described by a `.crugffi` sidecar transfers
  ownership and borrows through ordinary C calls, with no ABI change and no
  runtime metadata (Phase 9).
- Safe core types: `slice<T>` (a borrowed view with a checked `s[i]`),
  `option<T>`, `result<T,E>` and payload `enum`s with
  `some`/`none`/`ok`/`err`/variant constructors and a `match` statement.
  Plain C records with frozen layouts, so they cost what the equivalent struct
  costs (Phase 10).
- Ownership-aware optimization: an opt-in `restrict` fact from a proven
  `mut` or `own` parameter, with the caller contract checked at every call
  site and a byte-identical result to a hand-written `restrict` twin
  (Phase 11, ADR 0010).
- A conservative C-to-Crug migration tool, `c2crug`: it annotates only proven
  ownership, reports everything else as `raw` with evidence, and never claims
  the migrated program is memory-safe (Phase 12, ADR 0023).
- A platform abstraction layer: `libcrug` reaches an operating system only
  through `libcrug/pal/include/crug_pal.h`, a fake backend runs the runtime's
  contract suite with no OS at all, and a Linux backend maps the same twelve
  cases onto `pthread`/`futex`/`mmap`; the core names no system primitive
  (Phase 13, ADR 0011).
- `go` and `yield` with M:N scheduling: a task is a stackful fiber on one
  worker thread, its frame is copied at spawn, and an owned value crosses the
  boundary only through `move` (Phase 14, ADR 0008/0009/0013).
- `scope` for structured concurrency: a block that waits for every task it
  spawned and lets those tasks borrow what outlives the block (Phase 15,
  ADR 0024).
- `chan T`, `send`, `recv`, `close` and `select` with explicit ownership
  transfer on send: an owning element crosses only through `move`, and the
  receiver destroys it exactly once (Phase 16, ADR 0025).
- `Send`/`Sync` checks at task and channel boundaries: a raw pointer cannot be
  captured or sent, a scoped shared borrow needs `Sync`, and a thread-affine
  FFI handle stays on its thread (Phase 17, ADR 0026).
- A portable runtime: one scheduler core on Linux x86-64, Linux AArch64,
  Windows x86-64 and macOS ARM64, with the OS and CPU kept as two independent
  axes and one PAL contract suite every backend must pass (Phase 18, ADR 0012).
- `-fcrug-kernel` for freestanding use with no hidden runtime at all.

## Building

The fork tracks GCC 16.2.0, pinned in
[docs/crug/GCC-BASELINE.md](docs/crug/GCC-BASELINE.md) by checksum and imported
as the tag `releases/gcc-16.2`. The top-level [Makefile](Makefile) reproduces
that baseline:

    make baseline      # verify the tarball, fetch prerequisites, configure, build
    make help          # every target
    make check-host    # required host tools, read-only

The source tree is already in the repository, so a build does not unpack the
tarball: `make baseline` fetches GCC's own GMP/MPFR/MPC/ISL prerequisites (they
unpack at the top level and are ignored) and builds `all-gcc` out of tree into
`build/obj`. Later runs are incremental; after a source change use
`make reconfigure` when configure inputs changed.

`make baseline` stops at the compiler binaries: `xgcc` compiles (`-S`, `-c`)
but linking needs the target libraries built by `make build-full`.

`make install PREFIX=...` installs the compiler.

`make check-c` runs the `gcc.dg` C testsuite; it needs DejaGnu, which
`scripts/bootstrap-dejagnu.sh` installs into `tools/` without root. Nothing
generated is tracked.

The driver is `gcc/crugc`. It finds the build tree itself, so it can be used
from the repository root: `gcc/crugc -O2 -c file.c`.

`make check-C_COMPAT` compiles the `corpus/` programs with both `gcc/crugc` and
the baseline `xgcc` at `-O0`, `-O2` and `-Os` and fails on any difference in
exit status or object bytes; `scripts/diff-cc.sh` runs the same comparison with
custom flags.

`make check-KERNEL_COMPAT` runs the Phase 2 kernel/GNU C compatibility suite
and writes `out/` (ignored); see
[docs/crug/KERNEL-COMPATIBILITY.md](docs/crug/KERNEL-COMPATIBILITY.md).

`make check-CRUG_FLAGS` checks the `-fcrug-*` option set, its closure and the
feature macros, and `make check-GNU_C_COMPAT` recompiles the GNU C and kernel
corpora with the full flag set; both are described in
[docs/crug/FLAGS.md](docs/crug/FLAGS.md).

`make check-OWNERSHIP`, `make check-DROP`, `make check-BORROW`,
`make check-STRICT_UNSAFE` and `make check-FFI` run the ownership, destruction,
borrow and strict dg suites; `scripts/check-drop-*.sh` assert exactly-once
destruction, sanitizer cleanliness and the absence of helper calls,
`scripts/check-borrow-*.sh` assert that a borrowed function compiles to the
same object as its plain-pointer twin, that a borrow ends at its last use in
the dump, and that six realistic programs compile and run,
`scripts/check-strict-*.sh` assert that strict mode changes no code while
rejecting each unanalysed operation.

`make check-SAFE_TYPES` runs the safe-type suite (77 expected passes),
`scripts/check-safe-types.sh` tallies it, `scripts/check-safe-bounds.sh`
asserts that an out-of-range `s[i]` traps while `-fno-crug-bounds-checks`
removes the trap, and `scripts/check-owned-payloads.sh` asserts that an owning
payload enum destroys the active variant exactly once and never an inactive
one.

`make check-ALIAS_FACTS` runs the alias-fact dg suite,
`make check-OPTIMIZATION` adds the golden alias dump, the byte-identity and
adversarial checks and the LTO pass, `scripts/check-stop-conditions.sh` audits
every derived fact against its source declaration, and `bench/run.py` runs the
three-arm matrix (`--matrix alias --check`, `--matrix alias --lto`,
`--plain-c --check`); `docs/crug/OPTIMIZATION.md` describes them.

`make check-C2CRUG` runs the migration tool's unit checks and the whole flow on
`migration/` (transform, compile with `crugc`, differential run against the
original output, report validation, churn band); `docs/crug/C2CRUG.md`
describes them.

`make check-PAL` builds and runs the twelve-case PAL contract suite on both
backends, `scripts/check-pal.sh` asserts backend parity and that the fake
backend links no system symbol, and `scripts/lint-no-os-apis.sh libcrug/core`
asserts that the core names no OS primitive; `docs/crug/PAL.md` describes them.

`make check-MULTI_OS` runs the contract, the task suite and three plain-C
channel programs under one command on the current host (and with
`TARGET=aarch64-linux` the AArch64 port); the Windows port cross-builds with
`x86_64-w64-mingw32-gcc` and `.github/workflows/multi-os.yml` runs the matrix
on Linux, Windows and macOS runners; `docs/crug/PORTABILITY.md` describes it.

`make check-KERNEL` runs the Phase 19 stages in order, the object diff against
the wrapped GCC driver and the kernel-scoped P0 ledger check; the stage script
is `scripts/kernel-stage.sh` and `docs/crug/KNOWN-ISSUES.md` is the defect
ledger the kernel gates read.

`make check-DIAGNOSTICS` compiles the fifty-class diagnostics corpus, compares
each case byte for byte with its golden and asserts the SARIF `ruleId` plus the
origin notes (`scripts/check-diagnostic-goldens.sh`,
`scripts/check-diagnostic-sarif.sh`;
[docs/crug/DIAGNOSTICS.md](docs/crug/DIAGNOSTICS.md) is the registry and the
"common errors and how to fix them" guide); `make check-docs-examples` compiles
and runs every marked example in
[docs/crug/MIGRATION-EXAMPLES.md](docs/crug/MIGRATION-EXAMPLES.md) and compares
its stdout.

`make check-PERFORMANCE` runs the five Phase 21 checks (zero cost, pristine
parity, recorded baseline, concurrency and channels, kernel codegen) and writes
`bench/results/<date>.md`; `make pristine-xgcc` builds the unmodified reference
compiler it compares against, and
[docs/crug/RELEASE-POLICY.md](docs/crug/RELEASE-POLICY.md) owns every number
the qualification enforces.

`make check-FUZZ` runs the six Phase 22 harnesses and `make check-SANITIZER`
runs the sanitizer arms ([docs/crug/FUZZING.md](docs/crug/FUZZING.md) is the
operator's guide); `scripts/check-p0-open.sh` and
`scripts/check-regressions.sh` enforce the defect ledger, and
`scripts/check-fuzz-nightly.sh` reports the nightly window.

`make check-DOCS` checks the 44-document set against
[docs/crug/REQUIRED-DOC-SECTIONS.md](docs/crug/REQUIRED-DOC-SECTIONS.md): each
document exists with a title, a `Status:` line and its required sections, and
no document presents a post-1.0 roadmap feature as present
(`scripts/check-docs.sh`);
[docs/crug/COMPATIBILITY-POLICY.md](docs/crug/COMPATIBILITY-POLICY.md) states
the compatibility promises the set implies.

`make package` writes the release tarball, the SPDX SBOM and the checksums into
`dist/`; `make package-image` builds `crug/pkg:<version>` from
`packaging/Dockerfile`; `make check-PACKAGING` runs the whole flow end to end,
including the three smoke programs inside the image.

`make check-RC` runs the Phase 25 release-candidate checks: it replays the gate
archives, the P0/P1 ledger, the regression map and the nightly window, then the
multi-os and FFI matrices, and prints the kernel and performance commands that
need the long budget.

`scripts/count-open-p1.sh --max=2` applies the release-policy threshold and
reads the waiver rows in [docs/crug/KNOWN-ISSUES.md](docs/crug/KNOWN-ISSUES.md)
section 4.

`make check-GOROUTINES` runs the `go`/`yield` dg suite, five runtime programs,
the 100000-task stress run and the `strace` clone count; `docs/crug/TASKS.md`
and `docs/crug/GOROUTINES.md` describe them.

`make check-SCOPE` runs 23 `scope` dg cases and eight group programs whose
counters must balance (`leaked task group state: 0`); `docs/crug/SCOPE.md`
describes them.

`make check-CHANNELS` runs 40 `chan`/`select` dg cases and seven channel
programs (buffered round trip, handoff, receive and send arms, an owned
transfer, close, fan-in); `docs/crug/CHANNELS.md` describes them.

`make check-SEND_SYNC` runs 14 `Send`/`Sync` dg cases plus the raw-pointer,
thread-affinity and FFI-annotation pairs; `docs/crug/SEND-SYNC.md` describes
them.

Runnable programs that need no harness live in [examples/](examples/README.md),
one per Phase 4-17 feature; `make examples` builds them all into
`build/examples/` and checks that the two rejected ones still fail with their
documented diagnostic code, and `make examples EXAMPLES=<dir>` runs the same
build over another directory of programs.

See [NOTICE](NOTICE) for the licensing consequence of deriving from GCC.

## Licence

GPL-3.0-or-later (see [LICENSE](LICENSE)). The imported `gcc/` tree keeps its
own GPLv3+/LGPLv3+ headers, and the runtime library is GPLv3-or-later without a
runtime exception for now. [NOTICE](NOTICE) has the per-path map and the
historical note about the initial MIT commit.
