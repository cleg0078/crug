# CRUG

C + Rust + Go = CRUG. An extension of GCC that brings Rust-style memory
management and Go-style concurrency to C code, with C ABI compatibility as a hard
requirement.

## Status

Early implementation. The repository contains specification documents,
architecture decisions, and the pinned GCC 16.2.0 tree that the fork modifies
(the repository root is the GCC top level; the compiler is `gcc/`; the import
is tagged `releases/gcc-16.2`). Phase 1 of the release gates is implemented:
`gcc/crugc` is the CRUG driver, which for now behaves exactly like the GCC it
is built from, so plain C compiles and links identically. Phase 2 (kernel/GNU C
compatibility) is closed: the driver accepts `-fcrug-kernel` as a mode marker,
the kernel corpus and its no-runtime checks live in
`gcc/testsuite/crug.dg/kernel/`, and the `kernel-compat` CI job builds the pinned
baseline on every push and is a required status check on `main`. Phase 3
(feature flags) is closed: ten `-fcrug-*` options, their closure and the feature
macros - see [docs/crug/FLAGS.md](docs/crug/FLAGS.md). Phase 4 (ownership MVP)
implements `own` and `move`: qualifiers are erased from the type, `move` lowers
to a register copy, use-after-move and implicit copy are compile errors, and
`-fcrug-dump=ownership` prints the post-join state - see
[docs/crug/OWNERSHIP.md](docs/crug/OWNERSHIP.md). Phase 5 (automatic
destruction) implements `adopt`, `release`, `drop` and drop insertion on every
exit path, with `setjmp` and never-adopted values diagnosed - see
[docs/crug/DROP.md](docs/crug/DROP.md). Phase 6 (lexical borrow checker)
implements the `ref` and `mut` qualifiers and the `borrow`/`borrow_mut`
intrinsics: many shared borrows or exactly one mutable borrow, a lexical
lifetime, and compile errors for every conflicting use - see
[docs/crug/BORROW.md](docs/crug/BORROW.md). Phase 7 (non-lexical lifetimes)
makes a borrow end at its last use and fixes the interprocedural policy at depth
zero: a borrow may cross a call only as a `ref`/`mut` argument and may not be
returned or stored - see [docs/crug/BORROW.md](docs/crug/BORROW.md) and ADRs
[0019](docs/crug/adr/0019-interprocedural-borrow-policy.md) and
[0020](docs/crug/adr/0020-last-use-liveness.md). Phase 8 (strict mode) adds
`raw` pointers and `unsafe` blocks: `-fcrug-strict` rejects unanalysed
operations outside an `unsafe` block, with stable `crug-SAFE-*` codes, and
never changes generated code - see [docs/crug/STRICT.md](docs/crug/STRICT.md)
and [ADR 0021](docs/crug/adr/0021-strict-mode-policy.md). Phase 9 (C FFI
contracts) reads `.crugffi` sidecars: an annotated library hands out owned
values with a known destructor, consuming functions require `move`, and
`-fcrug-ffi-discover` finds a sidecar next to a quoted include. Metadata never
changes codegen - see [docs/crug/FFI.md](docs/crug/FFI.md) and
[ADR 0007](docs/crug/adr/0007-ffi-metadata-format.md). Phase 10 adds the safe
core types: `slice<T>` with checked `s[i]` indexing, `option<T>`,
`result<T,E>`, payload enums, their value constructors and a `match` statement
over all three sum shapes. They are ordinary C aggregates with frozen layouts,
no runtime and no helper calls - see
[docs/crug/SAFE-TYPES.md](docs/crug/SAFE-TYPES.md) and the frozen rows of
[docs/crug/ABI.md](docs/crug/ABI.md). A payload may be owned
(`own char *`): it is transferred into the constructor, dropped only when its
variant is active, and moved into `match` (`crug-ADT-015`/`-018`/`-019`/`-020`).
The Phase 10 gate passes 10/10 at `43996fe` (logs under
`docs/crug/gates/phase-10/43996fe/`). Phase 11 makes ownership visible to the
optimizer: with the opt-in `-fcrug-alias-facts`, a `mut` parameter (an
exclusive borrow) and an `own` parameter (the only owner) are compiled as
`restrict`, and the caller must pass `borrow_mut`/`borrow` - a plain
pointer at a `mut` parameter is rejected (`crug-BORROW-021`), as are two
views of one lender in a single call. The phase rests on byte identity: the
Crug kernel objects of the `bench/` matrix are identical to their
hand-written `restrict` twins at `-O2`/`-O3`, so the optimizer receives
exactly the fact the source proves and nothing more - see
[docs/crug/OPTIMIZATION.md](docs/crug/OPTIMIZATION.md) and
[ADR 0010](docs/crug/adr/0010-noalias-lowering.md). The Phase 11 gate passes
7/7 at `135dea9` (logs under `docs/crug/gates/phase-11/135dea9/`). Phase 12 adds
`c2crug`, a migration tool: it inserts `own` only where a local structural proof
shows exactly one release and no escape, rewrites that release to `drop`, keeps
every other line byte-identical and reports the case as `raw`; the phase rests on
a corpus with its own green test suite (`migration/`) and a machine-readable
report - see [docs/crug/C2CRUG.md](docs/crug/C2CRUG.md) and
[ADR 0023](docs/crug/adr/0023-c2crug-migration.md). The Phase 12 gate passes 7/7 at
`3aa78e7` (logs under `docs/crug/gates/phase-12/3aa78e7/`). Phase 13 adds `libcrug`'s
platform abstraction layer: one header (`libcrug/pal/include/crug_pal.h`) is the only
way the runtime may touch an operating system, two backends implement it (a fake one
with no OS at all and a Linux one on `pthread`/`futex`/`mmap`), and a twelve-case
contract suite runs against both so a port is a backend and not a fork - see
[docs/crug/PAL.md](docs/crug/PAL.md) and
[ADR 0011](docs/crug/adr/0011-pal-contract.md). The Phase 13 gate passes 7/7 at
`e8a0b22` (logs under `docs/crug/gates/phase-13/e8a0b22/`). Phase 14 adds the
goroutine runtime: `go f (args);` lowers to a per-call-site frame struct, a
static thunk and one `crug_task_spawn` call, M tasks run on N worker threads
through the PAL, each task has a fixed stack with a guard page, and the capture
rules (CONC-008, CONC-010 to CONC-014) make ownership explicit at the task
boundary - see [docs/crug/TASKS.md](docs/crug/TASKS.md),
[docs/crug/GOROUTINES.md](docs/crug/GOROUTINES.md),
[ADR 0008](docs/crug/adr/0008-goroutine-stack-model.md),
[ADR 0009](docs/crug/adr/0009-scheduler-model.md) and
[ADR 0013](docs/crug/adr/0013-arch-context-switch-policy.md). The Phase 14 gate
passes 8/8 at `2ecfe35` (logs under `docs/crug/gates/phase-14/2ecfe35/`).
Phase 15 adds structured concurrency: `scope { ... }` is a task group, every
`go` inside the block joins it, and the block waits for every child before it
ends - on the normal path, on `return`, `break`, `continue` and `goto` out.
A scoped task may borrow a value that outlives the block (a mutable borrow must
still be exclusive), a `goto` into a scope body is rejected (`CONC-015`) and so
is a borrow of a value that dies before the scope (`CONC-016`) - see
[docs/crug/SCOPE.md](docs/crug/SCOPE.md) and
[ADR 0024](docs/crug/adr/0024-structured-scope.md). The Phase 15 gate passes
7/7 at `1f42cc7` (logs under `docs/crug/gates/phase-15/1f42cc7/`).
Phase 16 adds channels and `select`: `chan T` is one opaque pointer whatever
the element type is, `channel (n)` takes its type from the context, capacity 0
is a handoff, and a `select` arm is `case recv (ch)`, `case v = recv (ch)`,
`case send (ch, expr)` or `default` in any order with the default last. A send
of an owning element requires `move` (the receiver destroys it), a destructor
cannot cross a channel, and a `select` arm cannot bind a receive from an
owning-element channel - see [docs/crug/CHANNELS.md](docs/crug/CHANNELS.md)
and [ADR 0025](docs/crug/adr/0025-channels-and-select.md). See
[docs/crug/progress.md](docs/crug/progress.md) for what is decided, what is
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
- `-fcrug-kernel` for freestanding use with no hidden runtime at all.

## Building

The fork tracks GCC 16.2.0, pinned in [docs/crug/GCC-BASELINE.md](docs/crug/GCC-BASELINE.md)
by checksum and imported as the tag `releases/gcc-16.2`. The top-level
[Makefile](Makefile) reproduces that baseline:

    make baseline      # verify the tarball, fetch prerequisites, configure, build
    make help          # every target
    make check-host    # required host tools, read-only

The source tree is already in the repository, so a build does not unpack the
tarball: `make baseline` fetches GCC's own GMP/MPFR/MPC/ISL prerequisites (they
unpack at the top level and are ignored) and builds `all-gcc` out of tree into
`build/obj`. Later runs are incremental; after a source change use
`make reconfigure` when configure inputs changed. `make baseline` stops at the
compiler binaries: `xgcc` compiles (`-S`, `-c`) but linking needs the target
libraries built by `make build-full`. `make install PREFIX=...` installs the
compiler. `make check-c` runs the `gcc.dg` C testsuite; it needs DejaGnu, which
`scripts/bootstrap-dejagnu.sh` installs into `tools/` without root. Nothing
generated is tracked.

The driver is `gcc/crugc`. It finds the build tree itself, so it can be used
from the repository root: `gcc/crugc -O2 -c file.c`. `make check-C_COMPAT`
compiles the `corpus/` programs with both `gcc/crugc` and the baseline `xgcc`
at `-O0`, `-O2` and `-Os` and fails on any difference in exit status or object
bytes; `scripts/diff-cc.sh` runs the same comparison with custom flags.
`make check-KERNEL_COMPAT` runs the Phase 2 kernel/GNU C compatibility suite and
writes `out/` (ignored); see [docs/crug/KERNEL-COMPATIBILITY.md](docs/crug/KERNEL-COMPATIBILITY.md).
`make check-CRUG_FLAGS` checks the `-fcrug-*` option set, its closure and the
feature macros, and `make check-GNU_C_COMPAT` recompiles the GNU C and kernel
corpora with the full flag set; both are described in
[docs/crug/FLAGS.md](docs/crug/FLAGS.md). `make check-OWNERSHIP`,
`make check-DROP`, `make check-BORROW`, `make check-STRICT_UNSAFE` and
`make check-FFI` run the
ownership, destruction, borrow and strict dg suites;
`scripts/check-drop-*.sh` assert exactly-once destruction, sanitizer
cleanliness and the absence of helper calls, `scripts/check-borrow-*.sh` assert
that a borrowed function compiles to the same object as its plain-pointer twin,
that a borrow ends at its last use in the dump, and that six realistic programs
compile and run, `scripts/check-strict-*.sh` assert that strict mode
changes no code while rejecting each unanalysed operation. `make check-SAFE_TYPES`
runs the safe-type suite (77 expected passes), `scripts/check-safe-types.sh` tallies it,
`scripts/check-safe-bounds.sh` asserts that an out-of-range `s[i]` traps while
`-fno-crug-bounds-checks` removes the trap, and `scripts/check-owned-payloads.sh`
asserts that an owning payload enum destroys the active variant exactly once and
never an inactive one. `make check-ALIAS_FACTS` runs the alias-fact dg suite,
`make check-OPTIMIZATION` adds the golden alias dump, the byte-identity and
adversarial checks and the LTO pass, `scripts/check-stop-conditions.sh` audits
every derived fact against its source declaration, and `bench/run.py` runs the
three-arm matrix (`--matrix alias --check`, `--matrix alias --lto`,
`--plain-c --check`); `docs/crug/OPTIMIZATION.md` describes them. `make
check-C2CRUG` runs the migration tool's unit checks and the whole flow on
`migration/` (transform, compile with `crugc`, differential run against the
original output, report validation, churn band); `docs/crug/C2CRUG.md` describes
them. `make check-PAL` builds and runs the twelve-case PAL contract suite on
both backends, `scripts/check-pal.sh` asserts backend parity and that the fake
backend links no system symbol, and `scripts/lint-no-os-apis.sh libcrug/core`
asserts that the core names no OS primitive; `docs/crug/PAL.md` describes them.
`make check-GOROUTINES` runs the `go`/`yield` dg suite, five runtime programs,
the 100000-task stress run and the `strace` clone count;
`docs/crug/TASKS.md` and `docs/crug/GOROUTINES.md` describe them.
`make check-SCOPE` runs 23 `scope` dg cases and eight group programs whose
counters must balance (`leaked task group state: 0`); `docs/crug/SCOPE.md`
describes them. `make check-CHANNELS` runs 39 `chan`/`select` dg cases and seven
channel programs (buffered round trip, handoff, receive and send arms, an owned
transfer, close, fan-in); `docs/crug/CHANNELS.md` describes them.
Runnable programs that need no harness live in
[examples/](examples/README.md), one per Phase 4-16 feature.
See [NOTICE](NOTICE) for the licensing consequence of deriving from GCC.

## Licence

GPL-3.0-or-later (see [LICENSE](LICENSE)). The imported `gcc/` tree keeps its
own GPLv3+/LGPLv3+ headers, and the runtime library is GPLv3-or-later without a
runtime exception for now. [NOTICE](NOTICE) has the per-path map and the
historical note about the initial MIT commit.
