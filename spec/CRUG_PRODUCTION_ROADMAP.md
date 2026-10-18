# CRUG — Production Compiler Roadmap

**Document type:** Execution roadmap  
**Audience:** AI development harness, compiler engineers, runtime engineers, reviewers, release engineers  
**Goal:** Take Crug from GCC fork to a production-ready 1.0 compiler  
**Baseline:** GCC 16.2 fork  
**Related specifications:** `CRUG_MASTER_SPEC_EN.md`, `C2CRUG_MASTER_IMPLEMENTATION_PLAN.md`  
**Core constraint:** **Compile existing Linux kernel C semantics without runtime overhead**  
**Core compatibility principle:** Crug features are opt-in; plain C remains plain C

---

# 1. Purpose

This document defines the implementation sequence required to move Crug from an experimental GCC extension to a production-ready compiler.

It is not a language reference.

It is an **execution plan**.

Every phase has:

- scope;
- prerequisites;
- required work;
- required tests;
- milestone;
- acceptance criteria;
- exit gate;
- non-goals.

The AI harness must not skip a gate merely because the next phase appears implementable.

A later phase may begin experimentally before a previous phase is fully closed, but **production status cannot advance past an unmet blocking gate**.

---

# 2. Production-Ready Definition

Crug 1.0 is production-ready only when all of the following are true.

## 2.1 C Compatibility

```text
[ ] Plain C mode preserves GCC C semantics for the supported dialects.
[ ] GNU C compatibility is sufficiently high for real low-level code.
[ ] Existing C headers remain usable.
[ ] Existing C static/shared libraries remain linkable.
[ ] C ABI is preserved for ABI-transparent Crug constructs.
[ ] Crug features disabled means no hidden runtime semantics.
```

## 2.2 Kernel / Freestanding

```text
[ ] Existing Linux-kernel-style GNU C semantics compile.
[ ] -fcrug-kernel adds no mandatory libcrug dependency.
[ ] No hidden worker threads.
[ ] No hidden heap allocations.
[ ] No hidden TLS.
[ ] No hidden locks.
[ ] No hidden bounds checks in plain C.
[ ] No unexpected __crug_* symbols in plain kernel objects.
[ ] Selected kernel subsystems compile.
[ ] Full kernel build succeeds on the primary architecture.
[ ] Boot smoke test succeeds.
```

## 2.3 Ownership Safety

```text
[ ] own is ABI-transparent.
[ ] move is zero-cost.
[ ] use-after-move is rejected.
[ ] double move is rejected.
[ ] implicit owned copy is rejected.
[ ] drops execute exactly once on supported paths.
[ ] conditional ownership state is analyzed correctly.
[ ] partial initialization is correctly handled or explicitly rejected.
```

## 2.4 Borrowing

```text
[ ] shared borrow rules are enforced.
[ ] mutable borrow exclusivity is enforced.
[ ] move/drop while borrowed is rejected.
[ ] NLL/last-use analysis works on real control flow.
[ ] borrowed return lifetime rules are documented and tested.
[ ] unsafe/raw boundaries are explicit in strict mode.
```

## 2.5 FFI

```text
[ ] Existing C libraries require no recompilation.
[ ] Unannotated C remains usable as unmanaged C.
[ ] .crugffi contracts can describe common ownership patterns.
[ ] libc integration is tested.
[ ] At least several real external libraries are validated.
```

## 2.6 Runtime / Concurrency

```text
[ ] libcrug core is OS-independent.
[ ] PAL contract is stable.
[ ] No OS API leaks into libcrug/core.
[ ] Goroutines/tasks are M:N rather than one-thread-per-task.
[ ] Structured concurrency works.
[ ] Channels work.
[ ] select works.
[ ] Owned values can move through tasks/channels.
[ ] Send/Sync-like checks prevent invalid transfers.
```

## 2.7 Portability

```text
[ ] Linux x86-64 supported.
[ ] Linux AArch64 supported.
[ ] Windows x86-64 supported.
[ ] macOS ARM64 supported.
[ ] At least one BSD backend validated or explicitly deferred by 1.0 policy.
[ ] Bare-metal/freestanding ownership compile path validated.
```

## 2.8 Optimization

```text
[ ] Crug ownership does not impose hidden runtime overhead.
[ ] move/borrow generate no runtime calls.
[ ] noalias facts are only emitted when formally justified.
[ ] vectorization benefits are demonstrated in at least one Crug benchmark.
[ ] plain C codegen is not systematically degraded.
[ ] kernel-style C codegen parity is validated.
```

## 2.9 Quality

```text
[ ] Compiler has no known P0 correctness bug.
[ ] Compiler has no known P0 miscompilation bug.
[ ] Runtime has no known P0 memory corruption bug.
[ ] Fuzzing has run continuously for an agreed qualification window.
[ ] Sanitizer suites are clean for supported configurations.
[ ] Release diagnostics are stable and actionable.
[ ] CI is reproducible.
[ ] Documentation is complete enough for independent users.
```

---

# 3. Severity Model

Use the following bug severity model.

## P0 — Release Blocking

Examples:

- miscompilation;
- memory corruption in compiler/runtime;
- use-after-free accepted in safe Crug;
- double-drop generated;
- ABI corruption;
- plain C semantics changed unexpectedly;
- kernel mode links runtime unexpectedly;
- optimizer assumes false noalias;
- scheduler loses tasks;
- channel ownership duplicated.

No release candidate may ship with known P0 issues.

## P1 — Major

Examples:

- important valid program rejected;
- compiler ICE on common construct;
- serious performance regression;
- incorrect diagnostic that prevents migration;
- supported platform runtime instability.

## P2 — Moderate

Examples:

- incomplete diagnostic;
- unsupported corner case with clear error;
- suboptimal codegen;
- missing non-critical tooling.

## P3 — Minor

Examples:

- formatting issue;
- documentation typo;
- non-blocking UX issue.

---

# 4. Milestone Model

Production progress is measured by named milestones.

```text
M0   Reproducible GCC baseline
M1   Plain C compatibility baseline
M2   Linux-kernel/GNU-C baseline
M3   Contextual Crug syntax
M4   Ownership MVP
M5   Deterministic drop
M6   Borrow checker MVP
M7   NLL / realistic borrowing
M8   Strict/unsafe model
M9   C FFI production layer
M10  Safe core types
M11  Optimizer-aware ownership
M12  c2crug migration tooling
M13  PAL stable
M14  Goroutine runtime MVP
M15  Structured concurrency
M16  Channels/select
M17  Send/Sync concurrency safety
M18  Multi-OS runtime
M19  Kernel build milestone
M20  Tooling/diagnostics hardening
M21  Performance qualification
M22  Security/reliability qualification
M23  Release candidate
M24  Crug 1.0
```

---

# 5. Phase 0 — Repository, Governance, Reproducible GCC Baseline

## Objective

Create a stable, reproducible base before adding Crug behavior.

## Prerequisites

None.

## Work Items

1. Pin GCC 16.2 source.
2. Record exact upstream tag/commit.
3. Create Crug fork branch.
4. Define repository structure.
5. Add build documentation.
6. Add developer bootstrap scripts.
7. Add CI for Linux x86-64.
8. Record baseline GCC C tests.
9. Define coding standards.
10. Define ADR process.
11. Define patch/commit conventions.
12. Create `docs/crug/progress.md`.
13. Add reproducible build metadata.

## Required Repository Structure

```text
gcc/crug/
gcc/testsuite/crug.dg/
libcrug/
docs/crug/
docs/crug/adr/
scripts/
bench/
```

## Tests

```text
[ ] GCC C frontend builds.
[ ] hello.c compiles and runs.
[ ] selected gcc.dg tests pass.
[ ] CI result is reproducible.
```

## Milestone

**M0 — Reproducible GCC baseline**

## Acceptance Criteria

```text
[ ] Two clean machines can produce a working compiler from documented steps.
[ ] Baseline test results are committed/documented.
[ ] No Crug semantic changes exist yet.
[ ] No intentional codegen differences exist.
```

## Exit Gate

Do not add ownership syntax until M0 is complete.

---

# 6. Phase 1 — Plain C Compatibility Mode

## Objective

Create `crugc` without changing C semantics.

## Work Items

1. Add user-facing `crugc` driver.
2. Preserve standard GCC argument forwarding.
3. Add Crug feature-state infrastructure.
4. Ensure all features default OFF.
5. Add `-fdump-crug`.
6. Add C compatibility tests.
7. Add differential compile/run tests.
8. Verify no `libcrug` linkage.

## Required Behavior

```bash
crugc foo.c
```

must behave as plain C.

## Tests

```text
[ ] identifiers named own/ref/mut/go compile as ordinary identifiers.
[ ] normal C ABI preserved.
[ ] no __crug_* runtime symbols.
[ ] no hidden constructors.
[ ] no hidden TLS.
```

## Milestone

**M1 — Plain C compatibility baseline**

## Acceptance Criteria

```text
[ ] Representative GCC C tests pass under crugc.
[ ] Differential behavior matches GCC for selected corpus.
[ ] No Crug runtime dependency exists.
```

---

# 7. Phase 2 — Linux Kernel / GNU C Compatibility Baseline

## Objective

Establish the permanent kernel constraint before Crug syntax changes.

## Core Constraint

> **Compile existing Linux kernel C semantics without runtime overhead.**

## Work Items

1. Add `-fcrug-kernel`.
2. Add `scripts/test-kernel-compat.sh`.
3. Create kernel-style microtest corpus.
4. Test GNU statement expressions.
5. Test `typeof` / `__typeof__`.
6. Test `__auto_type`.
7. Test attributes.
8. Test section placement.
9. Test packed/aligned structures.
10. Test zero-length arrays.
11. Test flexible arrays.
12. Test computed goto.
13. Test inline asm.
14. Test `asm goto`.
15. Test volatile/MMIO patterns.
16. Test builtin atomics/barriers.
17. Test branch prediction helpers.
18. Test `container_of`-style macros.
19. Test `-ffreestanding`.
20. Test `-nostdlib`.
21. Scan objects for `__crug_*`.

## Required No-Overhead Conditions

```text
[ ] no libcrug
[ ] no worker runtime
[ ] no hidden malloc
[ ] no hidden TLS
[ ] no hidden lock
[ ] no hidden bounds check
[ ] no hidden startup
```

## Milestone

**M2 — Linux-kernel/GNU-C baseline**

## Acceptance Criteria

```text
[ ] All kernel syntax microtests pass.
[ ] Object symbol scans show no Crug runtime leakage.
[ ] Freestanding object build works.
[ ] Kernel compatibility CI job is mandatory.
```

## Exit Gate

Ownership parser work must not begin until this baseline is protected by CI.

---

# 8. Phase 3 — Feature Flags and Contextual Syntax Infrastructure

## Objective

Introduce Crug syntax without globally reserving C identifiers.

## Work Items

Add:

```text
-fcrug-ownership
-fcrug-borrow
-fcrug-strict
-fcrug-goroutines
-fcrug-channels
-fcrug-modern
```

Implement contextual recognition infrastructure.

## Tests

```c
int own = 1;
int go = 2;
```

must still work where grammar makes them identifiers.

## Milestone

**M3 — Contextual Crug syntax**

## Acceptance Criteria

```text
[ ] Feature flags parse.
[ ] Feature flags do not affect code when syntax is unused.
[ ] own/go/etc. remain compatible contextual words.
[ ] Kernel macro regression remains clean.
```

---

# 9. Phase 4 — Ownership MVP (`own` + `move`)

## Objective

Prove zero-runtime unique ownership.

## Scope

Initial support:

- local pointer variables;
- direct assignments;
- function-local CFG;
- simple calls.

## Work Items

1. Parse `own`.
2. Store ownership metadata.
3. Parse `move(expr)`.
4. Define ownership state lattice.
5. Reject implicit owned copy.
6. Reject use-after-move.
7. Reject double move.
8. Add branch joins.
9. Add ownership dumps.
10. Add diagnostics.

## Required State Model

```text
UNINITIALIZED
OWNED
MOVED
MAYBE_MOVED
MAYBE_INITIALIZED
DROPPED
```

## Example

Valid:

```c
own Foo *a = create();
own Foo *b = move(a);
use(b);
```

Invalid:

```c
use(a);
```

## Runtime Requirement

```text
move = zero runtime calls
own = zero hidden metadata
```

## Milestone

**M4 — Ownership MVP**

## Acceptance Criteria

```text
[ ] own parses only when enabled.
[ ] own pointer ABI equals ordinary pointer ABI.
[ ] move produces no runtime helper.
[ ] implicit owned copy rejected.
[ ] use-after-move rejected.
[ ] double move rejected.
[ ] branch state merges are correct.
[ ] plain C/kernel regressions remain clean.
```

---

# 10. Phase 5 — Automatic Drop / RAII

## Objective

Automatically destroy owned resources exactly once.

## Work Items

1. Define destructor metadata.
2. Add built-in `malloc/free` contract.
3. Implement `adopt`.
4. Implement `release`.
5. Insert normal-scope drops.
6. Handle early return.
7. Handle branch exits.
8. Handle loops.
9. Define `goto` policy.
10. Define `switch` behavior.
11. Handle conditional initialization.
12. Add drop-order rules.
13. Add exactly-once tests.
14. Add ASan validation.

## Example

```c
own char *p = malloc(1024);
```

must eventually lower to equivalent manual C cleanup.

## Milestone

**M5 — Deterministic drop**

## Acceptance Criteria

```text
[ ] Every proven owned resource is dropped at most once.
[ ] Every required normal-path drop occurs.
[ ] move suppresses source drop.
[ ] adopted destructor executes correctly.
[ ] release suppresses automatic destruction.
[ ] return/branch cleanup is correct.
[ ] no runtime manager is required.
```

## Blocking Gate

No borrowing phase is considered production-grade until drop correctness is stable.

---

# 11. Phase 6 — Lexical Borrow Checker (`ref` / `mut`)

## Objective

Introduce basic Rust-like borrow rules.

## Work Items

1. Parse `ref`.
2. Parse `mut`.
3. Parse `borrow()`.
4. Parse `borrow_mut()`.
5. Track shared borrows.
6. Track mutable borrow.
7. Reject mutable+shared conflict.
8. Reject mutable+mutable conflict.
9. Reject move while borrowed.
10. Reject drop while borrowed.
11. Add parameter semantics.
12. Add basic returned-borrow rules.

## Milestone

**M6 — Borrow checker MVP**

## Acceptance Criteria

```text
[ ] multiple shared borrows allowed.
[ ] one mutable borrow allowed.
[ ] mut + ref rejected.
[ ] mut + mut rejected.
[ ] move/drop while borrowed rejected.
[ ] borrow has no refcount.
[ ] mut has no hidden lock.
```

---

# 12. Phase 7 — CFG Borrowing and Non-Lexical Lifetimes

## Objective

Make borrowing useful in realistic C control flow.

## Work Items

1. Build liveness analysis.
2. Track last use.
3. End borrow before lexical scope when safe.
4. Handle if/else joins.
5. Handle loops.
6. Handle early return.
7. Add provenance/path diagnostics.
8. Add limited interprocedural summaries.
9. Add regression corpus from real C patterns.

## Milestone

**M7 — NLL / realistic borrowing**

## Acceptance Criteria

```text
[ ] last-use ends borrow when safe.
[ ] mutable reborrow after dead shared borrow works.
[ ] branch joins remain sound.
[ ] loops do not create false unsoundness.
[ ] invalid escaping borrow rejected.
[ ] compiler does not ICE on real CFG patterns.
```

---

# 13. Phase 8 — Strict Mode, `raw`, and `unsafe`

## Objective

Create explicit safety boundaries.

## Work Items

1. Add `raw`.
2. Add `unsafe {}`.
3. Define raw dereference rules.
4. Define integer-pointer conversion rules.
5. Define owned-to-unmanaged conversion policy.
6. Define inline asm ownership policy.
7. Define `setjmp`/`longjmp` behavior.
8. Define unknown FFI behavior.
9. Add strict diagnostics.

## Milestone

**M8 — Strict/unsafe model**

## Acceptance Criteria

```text
[ ] unmanaged dangerous operations visible in strict mode.
[ ] unsafe has zero runtime cost.
[ ] hybrid C mode remains available.
[ ] kernel plain-C mode remains unchanged.
```

---

# 14. Phase 9 — Production C FFI Layer

## Objective

Use existing C libraries safely without recompiling them.

## Work Items

1. Finalize `.crugffi` grammar v1.
2. Implement metadata parser.
3. Add ownership-return contract.
4. Add consuming-argument contract.
5. Add shared-borrow contract.
6. Add mutable-borrow contract.
7. Add out-owned parameter contract.
8. Add nullability.
9. Add destructor metadata.
10. Add callback metadata.
11. Add blocking-call metadata placeholder.
12. Add thread-affinity metadata placeholder.
13. Add versioning.
14. Add diagnostics for conflicting metadata.

## Required Real Integrations

At minimum validate:

```text
libc
SQLite
libcurl or SDL
OpenSSL or FFmpeg
```

## Milestone

**M9 — C FFI production layer**

## Acceptance Criteria

```text
[ ] unannotated libraries remain usable.
[ ] annotated libraries gain ownership semantics.
[ ] metadata never changes binary ABI.
[ ] invalid metadata produces deterministic diagnostics.
[ ] existing .so/.a files need no recompilation.
```

---

# 15. Phase 10 — Safe Core Types

## Objective

Add common Rust-like safety abstractions.

## Subphases

### 10A — Slices

```c
slice<T>
```

Requirements:

- pointer + length;
- optional bounds checking;
- explicit C FFI lowering.

### 10B — `Option<T>`

Requirements:

- `some`;
- `none`;
- pattern matching;
- niche optimization later.

### 10C — `Result<T,E>`

Requirements:

- `ok`;
- `err`;
- explicit matching;
- `?` later.

### 10D — Algebraic Enums

Requirements:

- tagged variants;
- active-field drop;
- exhaustive match.

## Milestone

**M10 — Safe core types**

## Acceptance Criteria

```text
[ ] slice bounds checks are correct.
[ ] plain C pointers remain unaffected.
[ ] Option/Result layouts documented.
[ ] ADT drop logic correct.
[ ] match exhaustiveness enforced.
[ ] no ABI claim is made for Crug-native types unless documented.
```

---

# 16. Phase 11 — Ownership-Aware Optimization

## Objective

Exploit safe Crug facts without creating miscompilations.

## Work Items

1. Study GCC `restrict` representation.
2. Study points-to/alias APIs.
3. Define formal Crug alias guarantees.
4. Map proven `mut` exclusivity into GCC facts.
5. Map suitable unique ownership facts.
6. Add vectorization tests.
7. Add alias adversarial tests.
8. Add LTO tests.
9. Add optimizer dumps.
10. Add differential benchmarks.

## Critical Rule

A false noalias fact is a P0 compiler bug.

## Benchmark Matrix

```text
C ordinary
C restrict
Crug ref/mut
```

## Milestone

**M11 — Optimizer-aware ownership**

## Acceptance Criteria

```text
[ ] noalias facts are formally justified.
[ ] adversarial alias tests pass.
[ ] at least one benchmark reaches restrict-like optimization.
[ ] plain C codegen not degraded systematically.
[ ] LTO correctness preserved.
```

---

# 17. Phase 12 — `c2crug` Migration Tool Integration

## Objective

Make migration from C to Crug practical.

## Scope

Use the separate c2crug specification.

## Production Requirements

```text
[ ] analyze mode
[ ] safe mode
[ ] diff mode
[ ] confidence model
[ ] ownership inference
[ ] borrow suggestions
[ ] FFI metadata consumption
[ ] machine-readable report
[ ] no invented safety guarantees
```

## Required End-to-End Flow

```text
C project
   |
c2crug --safe
   |
Crug project
   |
crugc
   |
same tests / same behavior
```

## Milestone

**M12 — c2crug migration tooling**

## Acceptance Criteria

```text
[ ] safe mode transforms only proven cases.
[ ] migrated code compiles.
[ ] differential tests pass.
[ ] ambiguous ownership is preserved/raw/reported.
[ ] comments/source structure largely preserved.
```

---

# 18. Phase 13 — Platform Abstraction Layer (PAL)

## Objective

Create the portability boundary before adding sophisticated runtime behavior.

## Work Items

1. Freeze PAL v1 contract.
2. Create fake/test PAL.
3. Create Linux PAL.
4. Add thread abstraction.
5. Add wait/wake abstraction.
6. Add memory abstraction.
7. Add timer abstraction.
8. Add poller abstraction.
9. Add error mapping.
10. Add contract tests.
11. Add lint banning OS APIs in core.

## Milestone

**M13 — PAL stable**

## Acceptance Criteria

```text
[ ] scheduler core can compile against fake PAL.
[ ] Linux PAL passes contract tests.
[ ] no pthread/epoll/futex calls in libcrug/core.
[ ] architecture-dependent context code isolated.
```

---

# 19. Phase 14 — Goroutine / Task Runtime MVP

## Objective

Implement Go-like lightweight tasks.

## Work Items

1. Task structure.
2. Worker pool.
3. Task queues.
4. x86-64 SysV context switch.
5. Stack allocation through PAL.
6. Guard pages.
7. Spawn lowering.
8. Yield.
9. Task completion.
10. Runtime shutdown.
11. Error propagation baseline.
12. Stress tests.
13. Runtime statistics.

## Required Model

```text
M tasks
N OS workers
M >> N
```

## Milestone

**M14 — Goroutine runtime MVP**

## Acceptance Criteria

```text
[ ] go worker() does not create one OS thread per task.
[ ] 100k trivial tasks can be executed in stress testing.
[ ] no task loss.
[ ] no stack corruption.
[ ] scheduler passes ASan/UBSan.
[ ] plain C and kernel builds still link no runtime.
```

---

# 20. Phase 15 — Structured Concurrency

## Objective

Bound task lifetimes.

## Work Items

1. Implement task groups.
2. Implement `scope`.
3. Track child completion.
4. Define error propagation.
5. Define cancellation policy baseline.
6. Integrate borrowing.
7. Permit safe scoped borrows into child tasks.

## Milestone

**M15 — Structured concurrency**

## Acceptance Criteria

```text
[ ] scope waits all children.
[ ] child cannot outlive scope.
[ ] scoped borrow rules are sound.
[ ] owned captures transfer correctly.
[ ] no leaked task group state.
```

---

# 21. Phase 16 — Channels and `select`

## Objective

Provide Go-like typed communication.

## Subphase 16A — Channels

Implement:

```text
unbuffered
buffered
close
send
recv
ownership transfer
```

## Subphase 16B — Select

Implement:

```text
multiple send/recv cases
default
fairness policy
waiter cancellation
```

## Milestone

**M16 — Channels/select**

## Acceptance Criteria

```text
[ ] buffered/unbuffered channels correct.
[ ] blocked tasks resume correctly.
[ ] owned send transfers ownership exactly once.
[ ] close semantics documented.
[ ] select has no waiter leaks/UAF.
[ ] MPMC stress passes.
```

---

# 22. Phase 17 — Send/Sync-Like Static Concurrency Safety

## Objective

Prevent invalid cross-thread/task ownership transfers.

## Work Items

1. Define Send semantics.
2. Define Sync semantics.
3. Add type-property inference.
4. Add explicit overrides where justified.
5. Add FFI metadata.
6. Add thread-affinity properties.
7. Integrate with `go`.
8. Integrate with channels.
9. Add negative tests.

## Milestone

**M17 — Send/Sync concurrency safety**

## Acceptance Criteria

```text
[ ] non-sendable value cannot cross worker boundary.
[ ] non-sync shared borrow rejected.
[ ] thread-affine handles rejected when migration possible.
[ ] raw pointers do not gain false safety automatically.
```

---

# 23. Phase 18 — Multi-OS Runtime

## Objective

Prove portability architecture.

## Required Ports

### Linux x86-64

Already primary.

### Windows x86-64

Implement:

```text
threads
WaitOnAddress
VirtualAlloc
IOCP later
x86-64 Windows context
```

### macOS ARM64

Implement:

```text
pthread
mmap
kqueue
AArch64 context
```

### Linux AArch64

Validate PAL + AArch64 context.

### BSD

At least one BSD backend before or shortly after 1.0 depending release policy.

## Milestone

**M18 — Multi-OS runtime**

## Acceptance Criteria

```text
[ ] same scheduler core used on Linux, Windows, macOS.
[ ] PAL contract passes on each.
[ ] task/channel suites pass.
[ ] no platform-specific ifdef sprawl in core.
```

---

# 24. Phase 19 — Linux Kernel Build Qualification

## Objective

Move from syntax compatibility to real kernel compiler qualification.

## Stages

### K0 — Syntax Microtests

Already complete under M2.

### K1 — Selected Headers

Compile important header combinations.

### K2 — Translation Units

Compile selected source files.

### K3 — Subsystem

Compile one subsystem.

Candidate examples:

```text
lib/
kernel/
drivers subset
filesystem subset
```

### K4 — Broad Object Corpus

Compile a substantial cross-section.

### K5 — Full Kernel Build

Use:

```bash
make CC=crugc ...
```

or compatible wrapper.

### K6 — Boot Smoke Test

Boot a kernel compiled with Crug plain-C/kernel mode.

### K7 — Second Architecture

Repeat meaningful subset on AArch64.

## Milestone

**M19 — Linux kernel build milestone**

## Acceptance Criteria

For production 1.0:

```text
[ ] full primary-architecture kernel build succeeds.
[ ] kernel boots in smoke environment.
[ ] no mandatory Crug runtime symbols.
[ ] object-level regressions investigated.
[ ] no known P0 codegen issue in kernel corpus.
```

---

# 25. Phase 20 — Diagnostics, Tooling, Developer Experience

## Objective

Make Crug usable without compiler-internals expertise.

## Work Items

1. Stable diagnostic wording.
2. Rich move origin notes.
3. Rich borrow origin notes.
4. Path diagnostics.
5. Suggested fixes.
6. Dump documentation.
7. Error codes/categories.
8. IDE/LSP groundwork.
9. syntax highlighting grammar.
10. formatter decision.
11. debugger integration notes.
12. FFI tooling.
13. kernel compatibility tooling.

## Milestone

**M20 — Tooling/diagnostics hardening**

## Acceptance Criteria

```text
[ ] top 50 common errors have actionable diagnostics.
[ ] diagnostics include origin notes.
[ ] no common error requires reading GIMPLE dumps.
[ ] machine-readable diagnostics available.
[ ] docs include migration examples.
```

---

# 26. Phase 21 — Performance Qualification

## Objective

Demonstrate that Crug meets its performance philosophy.

## Required Benchmarks

### Compile-Time Features

Prove:

```text
own -> no runtime cost
move -> no runtime cost
borrow -> no refcount
mut -> no lock
```

### Plain C

Compare GCC vs Crug:

```text
-O0
-O2
-O3
LTO
```

### Kernel

Compare:

```text
object size
hot assembly
stack usage
symbols
```

### Concurrency

Compare:

```text
pthread-per-task
C thread pool
Crug task runtime
```

### Channels

Measure:

```text
latency
throughput
memory/task
scaling
```

## Milestone

**M21 — Performance qualification**

## Acceptance Criteria

```text
[ ] no systematic plain-C slowdown caused by Crug driver/frontend.
[ ] zero-cost claims validated in generated code.
[ ] concurrency performance is competitive with design goals.
[ ] no unexplained major regression remains.
```

No arbitrary "Crug must beat C" target is required.

---

# 27. Phase 22 — Security and Reliability Qualification

## Objective

Harden compiler and runtime before release candidate.

## Work Items

1. Continuous parser fuzzing.
2. Ownership-state fuzzing.
3. Borrow-checker fuzzing.
4. `.crugffi` fuzzing.
5. Scheduler fuzzing.
6. Channel state-machine fuzzing.
7. ASan runs.
8. UBSan runs.
9. TSan where runtime/fiber support is valid.
10. static analysis of runtime.
11. adversarial malformed-source corpus.
12. OOM/error-path tests.
13. concurrency stress.
14. fault injection.
15. reproducible compiler crashes corpus.
16. sanitizer hooks for fiber stacks.
17. miscompile differential tests.

## Qualification Window

Define a continuous clean window before RC, e.g.:

```text
30 days with no unresolved P0
```

The exact period is a release-management decision.

## Milestone

**M22 — Security/reliability qualification**

## Acceptance Criteria

```text
[ ] zero known P0.
[ ] fuzzing infrastructure continuously running.
[ ] sanitizer suites clean or exceptions documented.
[ ] stress suites stable.
[ ] all historical P0/P1 compiler bugs have regression tests.
```

---

# 28. Phase 23 — Documentation and Compatibility Policy

## Objective

Make the compiler maintainable and usable externally.

## Required Documents

```text
LANGUAGE.md
GRAMMAR.md
OWNERSHIP.md
BORROWING.md
UNSAFE.md
C-FFI.md
ABI.md
KERNEL-COMPATIBILITY.md
FREESTANDING.md
CONCURRENCY.md
CHANNELS.md
PORTABILITY.md
PAL.md
OPTIMIZATION.md
TESTING.md
COMPILER-INTERNALS.md
MIGRATION.md
RELEASE-POLICY.md
COMPATIBILITY-POLICY.md
```

## Compatibility Policy Must Define

```text
language versioning
Crug ABI versioning
.crugffi versioning
libcrug ABI policy
feature deprecation
GCC baseline policy
supported platforms
supported C dialects
kernel support policy
```

## Milestone

Part of **M23 — Release candidate readiness**

---

# 29. Phase 24 — Packaging and Distribution

## Objective

Make Crug installable and reproducible.

## Required Outputs

```text
source release
Linux binary package
Windows package
macOS package
checksums
SBOM
license notices
build instructions
container/CI image where appropriate
```

Optional:

```text
apt/rpm
Homebrew
Chocolatey/winget
```

## Acceptance Criteria

```text
[ ] clean machine install works.
[ ] hello C works.
[ ] hello Crug ownership works.
[ ] runtime sample works.
[ ] version information complete.
```

---

# 30. Phase 25 — Release Candidate

## Objective

Freeze semantics and validate production readiness.

## RC Rules

During RC:

- no large architecture changes;
- no syntax additions without release-blocking reason;
- bug fixes only;
- benchmark reruns;
- full regression;
- full kernel test;
- multi-OS runtime tests;
- c2crug safe migration tests;
- documentation freeze.

## Milestone

**M23 — Crug 1.0 Release Candidate**

## Acceptance Criteria

```text
[ ] all prior milestone blockers closed.
[ ] zero known P0.
[ ] agreed P1 threshold satisfied.
[ ] full primary kernel build passes.
[ ] kernel boot smoke passes.
[ ] Linux/Windows/macOS runtime suites pass.
[ ] FFI integrations pass.
[ ] performance qualification signed off.
[ ] docs complete.
```

---

# 31. Phase 26 — Crug 1.0

## Milestone

**M24 — Crug 1.0**

A 1.0 release is allowed only when all production-ready criteria in Section 2 are satisfied.

## Release Artifacts

```text
crugc
libcrug components
headers
.crugffi standard library metadata
documentation
tests
c2crug compatible release
release notes
compatibility statement
```

## Release Statement

Crug 1.0 should be able to truthfully claim:

```text
- GCC-based C-compatible systems language
- optional zero-cost ownership and borrowing
- optional Go-like structured concurrency
- C ABI interoperability
- multi-OS runtime portability
- Linux-kernel-style C compatibility
- no mandatory runtime overhead for plain/kernel C
```

---

# 32. Post-1.0 Roadmap

Potential 1.x / 2.0 work:

```text
generics stabilization
traits/interfaces
shared ownership
better kernel ownership contracts
RCU-aware modeling research
kernel task backend research
advanced io_uring runtime
growable stacks
additional BSDs
RISC-V runtime
embedded profiles
GPU address-space ownership
AI/HPC memory spaces
enhanced c2crug
LSP
formatter
package ecosystem
```

These are not prerequisites for 1.0 unless moved into scope by an ADR.

---

# 33. Dependency Graph

```text
M0
 |
M1
 |
M2
 |
M3
 |
M4
 |
M5
 |
M6
 |
M7
 |
M8
 |
M9
 +------> M10
 |          |
 +------> M11
 |
 +------> M12

M13
 |
M14
 |
M15
 |
M16
 |
M17
 |
M18

M2 -----------------------> M19
M4/M5 optional kernel own -> M19

M11 + M18 + M19
        |
       M20
        |
       M21
        |
       M22
        |
       M23
        |
       M24
```

Some work may overlap, but release gates remain ordered.

---

# 34. Suggested Parallel Workstreams

Once M5 is stable, work can split.

## Compiler Semantics Track

```text
borrow
NLL
unsafe
FFI
safe types
optimizer
```

## Runtime Track

```text
PAL
tasks
scope
channels
select
ports
```

## Kernel Track

```text
GNU C corpus
selected TUs
subsystem build
full build
boot
```

## Migration Track

```text
c2crug
FFI metadata
library integrations
```

## Quality Track

```text
CI
fuzzing
sanitizers
benchmarks
diagnostics
docs
```

An AI harness can work across tracks, but cross-track semantic changes require ADR review.

---

# 35. AI Harness Execution Rules

## 35.1 Never Skip Proof Steps

The AI must not conclude:

```text
"it compiles, therefore it is correct"
```

Required evidence may include:

```text
tests
IR
assembly
symbols
sanitizers
stress
differential behavior
```

## 35.2 Required Task Report

Every completed task must emit:

```text
Task:
Files changed:
Semantic change:
Runtime change:
ABI impact:
Kernel impact:
Tests added:
Tests run:
Failures:
Known limitations:
Next task:
```

## 35.3 Stop Conditions

The harness must stop phase advancement on:

```text
P0 miscompilation
ABI break
kernel runtime leakage
ownership unsoundness
double drop
false noalias
scheduler memory corruption
```

## 35.4 Architecture Escalation

Create an ADR rather than improvising when a task requires:

```text
new GIMPLE opcode
tree representation change
C ABI change
new runtime dependency
new kernel runtime dependency
new memory model rule
new concurrency rule
```

---

# 36. Required Regression Suites

Maintain independent suites:

```text
C_COMPAT
GNU_C_COMPAT
KERNEL_COMPAT
OWNERSHIP
BORROW
DROP
STRICT_UNSAFE
FFI
SAFE_TYPES
OPTIMIZATION
PAL
GOROUTINES
SCOPE
CHANNELS
SELECT
SEND_SYNC
MULTI_OS
KERNEL_BUILD
C2CRUG
FUZZ
SANITIZER
PERFORMANCE
```

A production release requires all mandatory suites green.

---

# 37. Release Blocking Matrix

| Failure | Blocks Prototype | Blocks Beta | Blocks 1.0 |
|---|---:|---:|---:|
| Plain C regression | Yes | Yes | Yes |
| Kernel runtime leakage | Yes | Yes | Yes |
| Ownership use-after-move accepted | Yes | Yes | Yes |
| Drop double free | Yes | Yes | Yes |
| Borrow false negative | No early | Yes | Yes |
| FFI metadata bug | No early | Yes | Yes |
| Goroutine task loss | No before runtime | Yes | Yes |
| Windows runtime failure | No early | Policy | Yes if supported |
| Kernel full build failure | No early | No | Yes |
| Kernel boot failure | No early | No | Yes |
| Major performance regression | No early | Yes | Yes |
| Known miscompilation | Yes | Yes | Yes |

---

# 38. Prototype / Alpha / Beta / Production Definitions

## Prototype

Expected features:

```text
plain C
kernel baseline
own
move
simple drop
```

Not production safe.

## Alpha

Expected:

```text
borrow MVP
FFI
safe types partial
runtime experimental
```

API may change.

## Beta

Expected:

```text
ownership stable
borrow/NLL stable
FFI stable
PAL stable
goroutines/channels stable
multi-OS mostly complete
kernel subsystem/full-build work advanced
```

No intentional language redesign.

## Release Candidate

Feature frozen.

Focus:

```text
correctness
compatibility
performance
documentation
packaging
```

## Production / 1.0

All Section 2 gates satisfied.

---

# 39. Minimal Prototype Milestone Plan

For an aggressive AI-assisted first prototype:

## Week 1

```text
M0 partial
M1
M2 microtests
driver
feature flags
```

## Week 2

```text
contextual own
ownership metadata
move parser
```

## Week 3

```text
local state analysis
use-after-move
double-move
diagnostics
```

## Week 4

```text
simple drop
malloc/free
early return
tests
kernel no-runtime regression
```

Prototype success:

```text
[ ] C still works
[ ] kernel-style C baseline still works
[ ] own works
[ ] move works
[ ] use-after-move rejected
[ ] simple automatic drop works
[ ] no runtime required
```

This prototype is a proof of architecture, not a production compiler.

---

# 40. Production Readiness Checklist

Before tagging 1.0:

## Compiler

```text
[ ] no P0
[ ] no unresolved known miscompilation
[ ] clean C regression
[ ] clean GNU C regression
[ ] ownership stable
[ ] borrowing stable
[ ] strict mode stable
```

## Kernel

```text
[ ] GNU C kernel corpus clean
[ ] full primary kernel build
[ ] boot smoke
[ ] zero mandatory Crug runtime
[ ] no hidden allocation/lock/TLS
```

## FFI

```text
[ ] libc
[ ] SQLite
[ ] one graphics/media library
[ ] one complex resource library
[ ] metadata version stable
```

## Runtime

```text
[ ] PAL stable
[ ] Linux
[ ] Windows
[ ] macOS
[ ] goroutines
[ ] scope
[ ] channels
[ ] select
[ ] Send/Sync
```

## Optimization

```text
[ ] zero-cost assertions checked
[ ] noalias tests
[ ] LTO
[ ] kernel codegen parity
[ ] benchmark suite published
```

## Quality

```text
[ ] fuzzing
[ ] ASan
[ ] UBSan
[ ] TSan where applicable
[ ] stress suite
[ ] deterministic CI
```

## Ecosystem

```text
[ ] c2crug compatible
[ ] docs
[ ] examples
[ ] packages
[ ] release policy
[ ] compatibility policy
```

---

# 41. Final Rule

The project must optimize for **trust**, not feature count.

A smaller Crug that correctly preserves C and proves ownership is more valuable than a feature-rich Crug that occasionally miscompiles code.

The production sequence is therefore:

```text
Compatibility
    ↓
Correctness
    ↓
Ownership
    ↓
Borrowing
    ↓
FFI
    ↓
Optimization
    ↓
Portable runtime
    ↓
Concurrency
    ↓
Kernel qualification
    ↓
Hardening
    ↓
1.0
```

The final production promise is:

> **Crug extends C without taking C away. It adds safety without forcing a managed runtime, concurrency without forcing garbage collection, and portability without hiding the machine.**
