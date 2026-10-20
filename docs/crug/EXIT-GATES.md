# CRUG — Phase Exit Gates (Phases 0–26)

**Document type:** Mechanical gate specification
**Status:** Accepted (2026-10-19) — ratified by CLEG0078 (project owner). Phase 0 and Phase 1 were adjudicated before ratification; their archived evidence is under `docs/crug/gates/phase-0/` and `docs/crug/gates/phase-1/`. Amendments are recorded in section 12.
**Owner:** CRUG project owner (compiler lead). Gate adjudicator: a second engineer who did not author the change, or a CI job that runs the checks verbatim.
**Derived from:** `spec/CRUG_PRODUCTION_ROADMAP.md` (phases 0–26, milestones M0–M24, severities P0–P3, regression suites §36, stop conditions §35.3), `spec/CRUG_MASTER_SPEC_EN.md` §56–§57, `spec/CRUG_RFC_0001.md` §7 (conformance classes C0/K0/O1/B1/F1/T1/C1/P1).
**Adds no requirements:** every CHECK here either restates a roadmap §5–§31 acceptance criterion as a command, or narrows a VAGUE criterion to its weakest defensible observable. Where they disagree the roadmap wins and this file is wrong.

## 1. Why this file exists

Roadmap §1 promises scope, prerequisites, required work, tests, milestone, acceptance criteria, exit gate and non-goals for every phase. As written, exactly two phases carry an `## Exit Gate` section (P0 §5, P2 §7) and one more a `## Blocking Gate` (P5 §10); exactly one carries `## Prerequisites` (P0 §5); no phase carries a non-goals section. Milestones are under-specified too: P23 is only "part of M23" (§28), P24 has no milestone (§29), and P23/P25 both claim M23.

## 2. How a gate is adjudicated

1. Run on a clean checkout of a recorded commit; an uncommitted tree adjudicates nothing.
2. `scripts/gate.sh phase-N` executes this file's CHECK lines for phase N verbatim, in order, stopping at the first mismatch.
3. A gate passes only if every CHECK matches the stated expected observable and the script exits 0.
4. Archive stdout, stderr, exit codes and the compiler version under `docs/crug/gates/phase-N/<commit>/` and commit the archive with the code. Evidence lives in the repo, not in CI history.
5. A `VAGUE:` statement is an open gate, not a pass. It stays open until the owner automates it or signs an ADR recording the manual procedure and its artefact.
6. Roadmap §35.3 stop conditions override every gate below: a P0 miscompilation, ABI break, kernel runtime leakage, ownership unsoundness, double drop, false noalias or scheduler memory corruption freezes phase advancement regardless of green checks.

## 3. The declaration rule

A gate is not satisfied by a declaration, anywhere: not by a commit message, a PR description, a release note, a `progress.md` tick, a dashboard, or the roadmap §35.2 task-report fields (`Tests run:`, `Failures:`). Those are claims by the party being judged. Only archived output of a command that exits 0 on a tree containing the work satisfies a gate. Corollary: if a CHECK's script, suite or corpus is absent from the repo, the gate is open by construction — "the tests are green" means nothing when the test does not exist.

## 4. Reading the CHECK lines

- `CHECK: <command or observable> => <expected>` — run from repo root; `=>` names the exact expected observable (exit code, diff count, symbol count, printed marker).
- `crugc` = `gcc/crugc`, the driver introduced by P1. `scripts/gate.sh` prepends the repository's `gcc/` directory to `PATH`, so the CHECK lines can use the bare name; outside the adjudicator use `gcc/crugc` (see docs/crug/DRIVER.md). `make check-<SUITE>` = the roadmap §36 suite of that exact name. `nm`/`objdump`/`readelf` = host binutils on the produced object or binary.
- Kernel build stages are written `stage K0`…`stage K7` (roadmap §24); conformance classes are written `class K0`, `class C0`, etc. (RFC 0001 §7). The token K0 is overloaded in the source documents; this file always disambiguates.

## 5. What fails today

At the commit introducing this file the repo holds only `spec/`, `README.md` and `LICENSE`: no `crugc`, no `gcc/crug/`, no `libcrug/`, no `gcc/testsuite/crug.dg/`. Every CHECK that invokes `crugc`, `make check-*`, `scripts/*.sh` or `libcrug/` therefore fails today. A CHECK that could pass before its phase is implemented is a defect in this file — report it rather than closing the gate.

## 6. Standing checks (every phase; not repeated per phase)

Master spec §57 requires parser tests, positive and negative semantic tests, diagnostics tests, C regression, kernel regression where relevant, ABI tests where relevant, runtime tests and sanitizer runs where relevant, optimization validation where optimization applies, documentation, and a `docs/crug/progress.md` update. A phase passes only when its own CHECKs and these standing checks pass, adjudicated through the roadmap §36 suite(s) the phase names.

## 7. Milestone resolution

```text
P0→M0   P1→M1   P2→M2   P3→M3   P4→M4   P5→M5   P6→M6   P7→M7   P8→M8
P9→M9   P10→M10 P11→M11 P12→M12 P13→M13 P14→M14 P15→M15 P16→M16 P17→M17
P18→M18 P19→M19 P20→M20 P21→M21 P22→M22
P23→none, M23 contributor (C1)   P24→none, M23 contributor (C2)
P25→M23 (closes)                 P26→M24
```

**C1 — Phase 23 vs Phase 25 both claim M23 (CONFLICT).** §28 says P23 is "Part of **M23 — Release candidate readiness**"; §30 assigns "**M23 — Crug 1.0 Release Candidate**" to P25. Adjudication: M23 is a multi-phase milestone closed only by P25; P23 closes no milestone of its own, and its gate is a precondition folded into P25's M23 gate.

**C2 — Phase 24 has no milestone (CONFLICT).** §29 names no milestone and §33's dependency graph jumps M23→M24. Adjudication: P24 is an M23 contributor on the same terms as P23 — M23 cannot be declared until P24 is green — and closes no milestone of its own.

**C3 — master spec §56 numbers phases differently.** It inserts 0K, 8K, 13B and 18K, and places ownership/noalias optimization at its P18. Roadmap numbering governs here; the orphan requirements are adjudicated inside roadmap phases: 0K→P2, 13B→P18, 18K→P21 (kernel codegen parity), 8K (kernel allocator ownership contracts) has no roadmap counterpart and is optional per §33 ("M4/M5 optional kernel own"), so it is a non-goal below and cannot block M19.

---

# Phase gates

## Phase 0 — Repository, Governance, Reproducible GCC Baseline
**Closes:** M0 — Reproducible GCC baseline.
**Prerequisite:** upstream GCC 16.2 source obtainable at tag `releases/gcc-16.2` with its tarball digest recorded (roadmap §5 says "None" — the only prerequisite is the external artefact plus a host bootstrap toolchain).
**Exit gate:**
- `CHECK: grep -q 'releases/gcc-16.2.0' docs/crug/GCC-BASELINE.md && make verify` => `gcc-16.2.0.tar.xz: OK`, exit 0 (`make` downloads into `sources/` on demand; upstream publishes SHA512, not SHA256).
- `CHECK: test -x build/obj/gcc/xgcc && build/obj/gcc/xgcc -Bbuild/obj/gcc/ --version | grep -q '16\.2\.0'` => exit 0 (`make baseline` must have completed; the gate does not rebuild).
- `CHECK: scripts/check-baseline.sh docs/crug/baseline/phase0` => exit 0, output `baseline phase0: PASS 224481 FAIL 84 XPASS 23 ...` (the archived summary is validated against `known-fail.txt`; the suite is not re-run by the gate, its full run is archived under that directory with a README naming the commit and compiler version, and any FAIL outside the reviewed allowlist fails this check).
- `CHECK: git diff --stat releases/gcc-16.2..HEAD -- gcc/` => empty output (no Crug semantic or codegen change).
- `CHECK: test -d gcc/crug && test -d gcc/testsuite/crug.dg && test -d docs/crug/adr && test -d scripts && test -d bench && test -s docs/crug/progress.md` => exit 0.
**Non-goals:** no `crugc` driver (P1), no feature flags (P3), no kernel mode (P2), no `libcrug` (P13), no ownership syntax; the Crug directories exist empty.
**Risk of skipping:** an unpinned, undocumented baseline makes every later differential, kernel and reproducibility claim untestable.

## Phase 1 — Plain C Compatibility Mode
**Closes:** M1 — Plain C compatibility baseline.
**Prerequisite:** Phase 0 gate archived; the baseline `xgcc` build tree and its recorded result set exist at the same commit.
**Exit gate:**
- `CHECK: scripts/diff-cc.sh corpus/` => exit 0, output `diff-cc: 30 files, 0 differing (-O2)` (object code and exit status of `gcc/crugc -O2` equal to baseline `xgcc -O2` over the named corpus).
- `CHECK: printf 'int own=1; int class_ref; int mut=2; int go=3;' | crugc -x c -fsyntax-only -` => exit 0 (contextual words still ordinary identifiers).
- `CHECK: scripts/check-no-hidden-state.sh` => exit 0, output `hidden-state: undefined-symbols=0 defined-symbols=0 dynamic-libcrug=0` (no hidden constructors, no TLS, no `libcrug`).
- `CHECK: make check-C_COMPAT` => exit 0, output `C_COMPAT: 3 option matrices plus 2 checks, 0 FAIL` (the C_COMPAT suite of roadmap section 36: the corpus at `-O0`, `-O2` and `-Os`, contextual words, and the hidden-state check).
**Non-goals:** no Crug syntax, no semantic flags (P3), no kernel mode (P2), no `libcrug` linkage, no diagnostics redesign (P20).
**Risk of skipping:** driver drift (extra defaults, changed include paths) silently alters plain-C semantics for every downstream test, and nobody can tell the difference later.
**Corpus (was `VAGUE`).** "Representative GCC C tests pass under crugc" fixed no corpus size. Adjudication: the named corpus is (a) `corpus/`, 30 programs covering C17 and the GNU C constructs the kernel mode depends on, compiled by both drivers at `-O0`, `-O2` and `-Os`; and (b) the full `gcc/testsuite/gcc.dg` run archived at `docs/crug/baseline/phase0/`, whose 224,481 passes and 84 allowlisted environmental failures are the pre-fork result of the same driver binary. `scripts/check-baseline.sh docs/crug/baseline/phase0` validates that archive and fails on any FAIL outside the allowlist. The owner still ratifies the corpus when ratifying this file (C-06).

## Phase 2 — Linux Kernel / GNU C Compatibility Baseline
**Closes:** M2 — Linux-kernel/GNU-C baseline (also the basis for `class K0`, RFC 0001 §7.2).
**Prerequisite:** Phase 1 gate archived (C_COMPAT green); `scripts/test-kernel-compat.sh`, `scripts/check-kernel-objects.sh`, `scripts/check-kernel-freestanding.sh`, the kernel-style microtest corpus under `gcc/testsuite/crug.dg/kernel/`, and `docs/crug/KERNEL-COMPATIBILITY.md` exist; `make check-KERNEL_COMPAT` is the local entry point.
**Exit gate:**
- `CHECK: bash scripts/test-kernel-compat.sh` => exit 0 across all 21 section 7 work items (statement expressions, `typeof`, `__auto_type`, attributes, sections, packed/zero-length/flexible arrays, computed goto, asm, `asm goto`, volatile/MMIO, atomics, `branch prediction`, `container_of`, `-ffreestanding`, `-nostdlib`, no-runtime scan), printed `kernel-compat: 21/21 work items PASS`.
- `CHECK: scripts/check-kernel-objects.sh out` => exit 0, printed `kernel-objects: <n> objects, __crug_=0, libcrug=0`.
- `CHECK: scripts/check-kernel-freestanding.sh` => exit 0, printed `freestanding: no runtime symbols, no init_array` (no `malloc`/`pthread_*`/`__cxa_*`, no `.init_array`).
- `CHECK: scripts/check-ci-gate.sh` => exit 0, printed `ci-gate: kernel-compat success=<sha> required=true` (the `kernel-compat` job concluded success on HEAD and is a required status check on the branch).
- Checks 2 and 3 consume the `out/` objects written by check 1, so they must run after it in order; `scripts/gate.sh` does. Check 4 needs `GH_TOKEN`/`GITHUB_TOKEN` and repository admin access for the protection endpoint, and is the only check that can only pass after the owner pushes and configures the repository (docs/crug/KERNEL-COMPATIBILITY.md section 6).
**Non-goals:** no real kernel source (P19), no ownership in kernel mode (master-spec 8K, optional per §33), no codegen-parity benchmarking (P21), no -O-level tuning.
**Risk of skipping:** syntax compatibility is assumed rather than measured, and later kernel build failures get misattributed to the optimizer or to ownership work.

## Phase 3 — Feature Flags and Contextual Syntax Infrastructure
**Closes:** M3 — Contextual Crug syntax.
**Prerequisite:** Phase 2 gate archived and the `kernel-compat` CI job mandatory at HEAD (roadmap §7: parser work must not begin until that baseline is CI-protected). Artefacts: `docs/crug/FLAGS.md`, `scripts/check-crug-flags.sh`, `scripts/check-gnu-c-compat.sh`, `scripts/diff-obj.sh` and the fixtures in `gcc/testsuite/crug.dg/flags/`.
**Exit gate:**
- `CHECK: scripts/check-crug-flags.sh` => exit 0, printed `crug-flags: <n>/<n> work items PASS`. Covers every flag alone, the exact feature-macro closure of each, every accepted pair, the three `-fcrug-kernel` conflicts, the unknown-option rejection, and the contextual-words fixture.
- `CHECK: scripts/diff-obj.sh corpus/ "-O2" "-O2 -fcrug-modern"` => exit 0, printed `diff-obj: 30 files, 0 differing (-O2 | -O2 -fcrug-modern)` (an unused flag must not change codegen).
- `CHECK: for f in -fcrug-ownership -fcrug-borrow -fcrug-slices -fcrug-result -fcrug-adt -fcrug-goroutines -fcrug-channels -fcrug-strict -fcrug-modern -fcrug-kernel; do crugc "$f" -fsyntax-only gcc/testsuite/crug.dg/flags/words-as-identifiers.c; done` => exit 0 (every contextual word stays an ordinary C identifier under every single flag).
- `CHECK: make check-GNU_C_COMPAT` => exit 0, printed `GNU_C_COMPAT: 50 files, 0 FAIL` (the 30-program GNU C corpus and the 18-file kernel corpus still compile with the flag sets on, plus the macro fixture).
**Non-goals:** flags gate parsing only — no ownership state (P4), no borrow rules (P6), no drop (P5), no goroutine lowering (P14), no channel types (P16).
**Risk of skipping:** globally reserving `own`/`ref`/`mut`/`go` breaks kernel macros and is a one-way C compatibility break.

## Phase 4 — Ownership MVP (`own` + `move`)
**Closes:** M4 — Ownership MVP.
**Prerequisite:** Phase 3 gate archived; `-fcrug-ownership` accepted by the flag table at the same commit.
**Exit gate:**
- `CHECK: crugc -O2 -fcrug-ownership -c gcc/testsuite/crug.dg/ownership/abi-ptr.c && test -z "$(readelf -sW abi-ptr.o | grep '__crug_' || true)"` => exit 0 (the `_Static_assert(sizeof(own Foo*)==sizeof(Foo*))` and the alignment assertions in that test compile, and the object carries no `__crug_` symbol).
- `CHECK: crugc -O2 -fcrug-ownership -c gcc/testsuite/crug.dg/ownership/move-zero-cost.c -o move-zero-cost.o && test "$(objdump -d move-zero-cost.o | grep -cE 'call|__crug_')" = 0` => exit 0 (move lowers to a register copy, no helper call).
- `CHECK: make check-OWNERSHIP` => exit 0, with at least one `dg-error` compile-fail case per rejection class: use-after-move, double move, implicit owned copy.
- `CHECK: crugc -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/ownership/use-after-move.c` => exit 1.
- `CHECK: crugc -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/ownership/use-after-move.c 2>&1 | grep -q '\[crug-UAM-001\]'` => exit 0 (the diagnostic carries the stable code, not free text).
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (every code referenced by docs/ and spec/ is registered, with no duplicate or unknown-area code).
- `CHECK: crugc -fcrug-ownership -fcrug-dump=ownership -c gcc/testsuite/crug.dg/ownership/branch-join.c && grep -q 'join .*: p=OWNED/MOVED -> MAYBE_MOVED' branch-join.crug-ownership` => exit 0 (the dump prints the post-join state for the variable).
**Non-goals:** no drop insertion (P5), no borrow (P6), no kernel-mode `own` (optional, §33), no cross-function ownership transfer beyond the "simple calls" scope, no destructor metadata.
**Risk of skipping:** an ownership state machine validated on the wrong pointer ABI leaks hidden metadata into every `own` variable and poisons P5–P11.

## Phase 5 — Automatic Drop / RAII
**Closes:** M5 — Deterministic drop.
**Prerequisite:** Phase 4 gate archived; the destructor decision is taken in ADR 0005 (Accepted 2026-10-18) and its amendments A-1..A-7, and §35.4's ADR requirement for representation changes is met by ADR 0003 D1/D5 (the destructor is analysis state, not a tree field).
**Exit gate:**
- `CHECK: scripts/check-drop-suite.sh` => exit 0, printed `drop-suite: <N> tests, 0 failures` (the DejaGnu suite over `crug.dg/drop/`: scope end, early return, branches, loops, `goto` in and out, `switch`, conditional initialization, move-suppresses-drop, adopt, release, and every rejection below).
- `CHECK: scripts/check-drop-exactly-once.sh` => exit 0, printed `exactly-once: 4/4 programs` (allocation/free counts through an in-test counting allocator: allocated=dropped, adopted=dropped, moved=1/1, released=1/0).
- `CHECK: scripts/check-drop-asan.sh` => exit 0, printed `drop-asan: 3/3 programs, 0 with reports` (no leak, double free or invalid free at `-O1`).
- `CHECK: scripts/check-drop-objdump.sh` => exit 0, printed `drop-objdump: 5 objects, 0 with __crug_ calls` (cleanup lowers to direct destructor/`free` calls, no runtime manager).
- `CHECK: scripts/check-drop-policy.sh` => exit 0, printed `drop-policy: goto=[crug-DROP-002] setjmp=[crug-DROP-003] unadopted=[crug-OWN-007] operand=[crug-DROP-001]` (each rejection is a stable code, not free text).
- `CHECK: scripts/check-drop-dump.sh` => exit 0, printed `drop-dump: scope-end return break continue goto 5 kinds` (`-fcrug-dump=drop` records each exit kind).
- `CHECK: scripts/check-diagnostics.sh` => exit 0, printed `check-diagnostics: 66 registered code(s); 66 referenced code(s); all references registered.`
**Non-goals:** no `ref`/`mut` (P6), no interprocedural drop elision, no `setjmp`/`longjmp` cleanup guarantees (policy only, P8), no kernel-mode drop lowering (optional §33).
**Risk of skipping:** double-drop is P0; every later borrow-soundness argument assumes drop executes exactly once.

## Phase 6 — Lexical Borrow Checker (`ref` / `mut`)
**Closes:** M6 — Borrow checker MVP.
**Prerequisite:** Phase 5 gate archived at the same commit — roadmap §10 blocking gate: no borrowing phase is production-grade until drop correctness is stable. `-fcrug-borrow` is accepted by the flag table at the same commit, and `docs/crug/BORROW.md` records the accepted subset.
**Exit gate:**
- `CHECK: scripts/check-borrow-suite.sh` => exit 0, printed `borrow-suite: 62 tests, 0 failures`
- `CHECK: scripts/check-borrow-cost.sh` => exit 0, printed `borrow-cost: 2 functions, 0 calls, identical objects`
- `CHECK: scripts/check-borrow-abi.sh` => exit 0, printed `borrow-abi: 4 assertions, 0 differences`
- `CHECK: crugc -O2 -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/borrow/mut-plus-ref.c` => exit 1
- `CHECK: scripts/check-borrow-policy.sh` => exit 0, printed `borrow-policy:` and the Phase 6 codes
- `CHECK: scripts/check-borrow-dump.sh` => exit 0, printed `borrow-dump: 2 shared, 1 mutable, 3 ends`
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (the registry stays consistent; later phases add codes)
**Non-goals:** no NLL/last-use (P7), no interprocedural lifetimes, no Send/Sync (P17), no runtime borrow tracking, no changes to plain-C pointer rules.
**Risk of skipping:** a borrow checker built before drop is exact will accept programs whose cleanup paths the IR cannot prove, producing UAF that reviewers trust because "borrow checked".

## Phase 7 — CFG Borrowing and Non-Lexical Lifetimes
**Closes:** M7 — NLL / realistic borrowing.
**Prerequisite:** Phase 6 gate archived; ADR 0019 (interprocedural borrow policy, depth zero, calls fail closed) and ADR 0020 (last-use liveness) accepted; the last-use analysis is visible in `-fcrug-dump=borrow` as `end ... (last-use)`.
**Exit gate:**
- `CHECK: scripts/check-borrow-suite.sh` => exit 0, printed `borrow-suite: 88 tests, 0 failures` (the NLL programs, the escape and calling-convention rejections, and the Phase 6 set).
- `CHECK: scripts/check-borrow-real-c.sh` => exit 0, printed `borrow-real-c: 6 programs, 0 ICE` (six realistic programs compile and run under the checker).
- `CHECK: scripts/check-borrow-escape.sh` => exit 0, printed `borrow-escape: return rejected at use site, origin noted`.
- `CHECK: scripts/check-borrow-nll.sh` => exit 0, printed a `borrow-nll:` line with at least one last-use end and one scope-end end.
- `CHECK: scripts/check-borrow-policy.sh` => exit 0, printed `borrow-policy: BORROW-002 BORROW-003 BORROW-004 BORROW-005 BORROW-007 BORROW-008 BORROW-018 BORROW-019 BORROW-020`.
- `CHECK: make check-C_COMPAT && make check-KERNEL_COMPAT` => exit 0 (no regression when borrow is off).
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (every cited code is registered).
**Non-goals:** no place-level lifetimes (fields and elements of an aggregate are owned or borrowed as a whole; ADR 0019), no reborrow of a borrow (deferred to Phase 12), no loop fixpoint beyond the conservative "every borrow is live inside a loop" rule, no unsafe-escape valve (P8), no optimizer use of borrow facts (P11).
**Risk of skipping:** a lexical-only checker rejects valid kernel-style code, and users ship with `-fcrug-borrow` off — the safety claim becomes decorative.
**Interprocedural policy:** depth zero. A borrow may be passed only to a `ref`/`mut` parameter (`crug-BORROW-020`); returning one (`crug-BORROW-019`) or storing it where it outlives its referent (`crug-BORROW-018`) is rejected. No function is summarized, so a call can never extend a borrow's lifetime; this fails closed and is recorded in ADR 0019.

## Phase 8 — Strict Mode, `raw`, and `unsafe`
**Closes:** M8 — Strict/unsafe model.
**Prerequisite:** Phase 7 gate archived; ADR 0021 (strict mode policy, Proposed) covering raw dereference, integer-pointer conversion, owned→unmanaged conversion, inline asm, `setjmp`/`longjmp` and unknown FFI; `docs/crug/STRICT.md`; the fixtures under `gcc/testsuite/crug.dg/strict/` and the four `scripts/check-strict-*.sh` runners.
**Exit gate:**
- `CHECK: scripts/check-strict-raw.sh` => exit 0, printed `strict-raw: hybrid exit 0, strict [crug-SAFE-001]`
- `CHECK: scripts/check-strict-codegen.sh` => exit 0, printed `strict-codegen: 4 programs, 0 differences`
- `CHECK: scripts/check-strict-suite.sh` => exit 0, printed `strict-suite: 21 tests, 0 failures`
- `CHECK: scripts/check-strict-policy.sh` => exit 0, printed `strict-policy: SAFE-001 SAFE-002 SAFE-003 SAFE-004 SAFE-005 OWN-011 OWN-012`
- `CHECK: make check-C_COMPAT` => exit 0
- `CHECK: make check-KERNEL_COMPAT` => exit 0 (plain kernel C unchanged when strict is off)
- `CHECK: scripts/check-diagnostics.sh` => exit 0
The first check compiles one fixture twice: without `-fcrug-strict` it must exit 0, with it it must exit 1 and print `[crug-SAFE-001]`. The second replaces the old directory `diff-obj` arm, because a raw or `unsafe` construct cannot be compiled with `-O2` alone, so the comparison is `-O2 -fcrug-ownership` against `-O2 -fcrug-strict` on a corpus that uses both. `scripts/check-strict-suite.sh` runs `make check-STRICT_UNSAFE` and reads the DejaGnu summary, so the check-3 policy cases (a `longjmp` rejection and an inline-asm rejection, each with the hybrid counterpart accepted) are inside the suite rather than named in the gate text.
**Non-goals:** no memory safety inside `unsafe`, no provenance or UB sanitizer, no `restrict`-style optimizer facts (P11), no removal of hybrid C mode.
**Risk of skipping:** without an explicit escape hatch users disable the whole checker at the first false positive, leaving no boundary to audit.

## Phase 9 — Production C FFI Layer
**Closes:** M9 — C FFI production layer.
**Prerequisite:** Phase 5 and Phase 8 gates archived; ADR 0007 (Accepted 2026-10-19) defines `.crugffi` v1; `docs/crug/FFI.md` documents the implemented layer; the contract fixtures live in `gcc/testsuite/crug.dg/ffi/phase9/` and the integration programs in `gcc/testsuite/crug.dg/ffi/integration/`.
**Exit gate:**
- `CHECK: make check-FFI` => exit 0, printed `ffi-suite: 19 cases, 0 failures`.
- `CHECK: scripts/check-ffi-integration.sh` => exit 0, printed `ffi-integration: 4 libraries unchanged, 4 programs OK`.
- `CHECK: scripts/check-ffi-conflict.sh` => exit 0, printed `ffi-conflict: crug-FFI-004 stable across 2 runs`.
- `CHECK: scripts/check-ffi-codegen.sh` => exit 0, printed `ffi-codegen: 2 files, 0 differences`.
- `CHECK: scripts/check-ffi-policy.sh` => exit 0, printed `ffi-policy: FFI-001 FFI-002 FFI-003 FFI-004 FFI-005 FFI-006 FFI-007 FFI-008 FFI-009 FFI-010 FFI-011`.
- `CHECK: scripts/check-diagnostics.sh` => exit 0.
**Non-goals:** no ABI changes to annotated libraries, no run-time metadata loading, no general binding generator, no blocking/thread-affinity enforcement (placeholder fields only; enforced at P17).
**Risk of skipping:** FFI metadata that perturbs codegen breaks the "existing .so/.a need no recompilation" promise and every integration claim with it.
`VAGUE:` "several real external libraries" is unspecified; weakest defensible check: exactly the four named in §14 (libc, SQLite, libcurl-or-SDL, OpenSSL-or-FFmpeg). The owner must not substitute easier libraries after the fact.

## Phase 10 — Safe Core Types
**Closes:** M10 — Safe core types.
**Prerequisite:** Phase 9 gate archived (slice/FFI lowering rules) and Phase 5 gate archived (destruction lowering for ADTs). Increment 1 exists at the commit that introduced amendment A-11: 10A slices, 10B `option<T>`, 10C `result<T,E>`, the value constructors, `match` over both sums, checked indexing and the frozen `ABI.md` rows (`SAFE-TYPES.md`). Increment 2a adds 10D payload enums and `match` over them, with the payload-enum `ABI.md` row frozen. Increment 2b adds owned payloads, the conditional drop of the active variant, the `match` transfer of an owned payload and the `crug-ADT-015` moved-scrutinee rule; the gate is adjudicable in full.
**Exit gate:**
- `CHECK: make check-SAFE_TYPES` => exit 0 with at least `# of expected passes 77` under `gcc/testsuite/crug.dg/safe-types/`: accepted programs compile and run at `-O2` under `-fcrug-slices`/`-fcrug-result`, every rejection is a `dg-error` case, `bounds-off.c` runs with `-fno-crug-bounds-checks`, and the payload-enum cases cover construction, a tagged payload enum, `match` over unit and payload variants, an owned payload destroyed exactly once, and the `crug-ADT-001`..`-004`, `-011`..`-013`, `-016`..`-020` rejections.
- `CHECK: scripts/check-safe-types.sh` => exit 0, printed `safe-types: 77 tests, 0 failures`.
- `CHECK: scripts/check-safe-bounds.sh` => exit 0, printed `safe-bounds: in-range 0, out-of-range SIGILL, unchecked no trap, plain C no trap` (checked `s[i]` carries the length comparison and `ud2`, `-fno-crug-bounds-checks` and a plain C array carry none).
- `CHECK: crugc -fsyntax-only -fcrug-slices -fcrug-result gcc/testsuite/crug.dg/safe-types/layout.c` => exit 0, where the file `_Static_assert`s every size, alignment and offset recorded in `docs/crug/ABI.md` section 3 for `slice`, `option` and `result`.
- `CHECK: crugc -fsyntax-only -fcrug-adt gcc/testsuite/crug.dg/safe-types/payload-enum-layout.c` => exit 0, where the file `_Static_assert`s the frozen payload-enum row: tag at offset 0, union at the next `_Alignof` boundary, variant members at offset 0 of the union, and the recorded sizes and alignments.
- `CHECK: crugc -fsyntax-only -fcrug-adt gcc/testsuite/crug.dg/safe-types/payload-enum-forward.c` => exit 1 with `[crug-ADT-017]`, so a payload enum cannot complete a forward tag reference (its tag denotes a record, not an `ENUMERAL_TYPE`).
- `CHECK: crugc -fsyntax-only -fcrug-result gcc/testsuite/crug.dg/safe-types/match-non-exhaustive.c` => exit 1 with an exhaustiveness diagnostic naming the missing variant and the stable code `[crug-ADT-011]`.
- `CHECK: scripts/check-owned-payloads.sh` => exit 0, printed `owned-payloads: active variant dropped once, inactive never, six unsafe forms rejected`: the `drop` dump of `adts-drop.c` has exactly one `via destroy_buf` line, it names `[Text]`, and it has no line for the statically inactive `Number`; the program counts exactly one destruction; the `-O0` assembly guards the destructor with the tag and a null test; and the six unsafe forms are rejected with `crug-ADT-018` (by value at a function boundary), `crug-ADT-019` (a payload that was not moved in), `crug-ADT-020` (an owned variant without a binding, and a `_` arm over it), `crug-ADT-015` (an owned scrutinee that was not moved) and `crug-UAM-003` (a copy).
- `CHECK: make check-C_COMPAT` => exit 0 (plain C pointers unaffected).
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (ADT-005/ADT-006/ADT-016..ADT-020 and TYPE-012/TYPE-013/TYPE-014 registered, no duplicate or unknown-area code).
**Non-goals:** no generics or traits (§32, post-1.0), no niche optimization (explicitly "later"), no `?` operator, no ABI stability promise for Crug-native types beyond the frozen rows of `ABI.md`, no mutable-slice distinction.
**Risk of skipping:** types shipped before their layout is written down become de-facto ABI that cannot be fixed without a 2.0.

## Phase 11 — Ownership-Aware Optimization
**Closes:** M11 — Optimizer-aware ownership.
**Prerequisite:** Phase 7 gate archived (proven `mut` exclusivity, NLL soundness), ADR 0010 (noalias lowering, Accepted; the 5% band is ratified there), and the three-arm benchmark matrix (C ordinary / C restrict / Crug mut-ref) exists under `bench/`.
**Exit gate:**
- `CHECK: bench/run.py --matrix alias --check` => exit 0: for every kernel at -O2 and -O3 the Crug kernel object is byte-identical to its hand-written `restrict` twin, and the measured crug/restrict ratio is ≤ 1.05 wherever the objects differ (identical objects are reported as 1.000).
- `CHECK: scripts/check-optimization.sh` => exit 0: the `-fdump-tree-alias` signatures of `noalias-golden.c` match `noalias-golden.expected` exactly (restrict on `mut`/`own` parameters only), 8/8 kernel objects are byte-identical, a ref-only function equals its plain twin, the flag alone changes nothing, and the five adversarial programs are rejected (BORROW-002, -003, -021, -022, -023).
- `CHECK: make check-OPTIMIZATION` => exit 0: 16 DejaGnu `crug.dg/alias` cases plus `scripts/check-optimization.sh`, `--matrix alias --check` and `--matrix alias --lto` (LTO build, equal results, execution).
- `CHECK: bench/run.py --plain-c --check` => exit 0: the plain kernel objects are byte-identical to `bench/baseline.json` at -O0/-O2/-O3 (a code change is then measured against the 1.02 band), and the LTO arm builds and runs correctly; LTO timing is not enforced because GCC's LTO objects are not byte-reproducible.
- `CHECK: scripts/check-stop-conditions.sh` => exit 0 printing `stop-conditions: N facts audited, 0 false-noalias reports` with N > 0 (a false noalias fact is a P0 and stops the phase).
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (BORROW-021, BORROW-022 and BORROW-023 registered, no duplicate or unknown-area code).
- `CHECK: make check-C_COMPAT` => exit 0 (plain C objects are unchanged when the flag is off).
**Non-goals:** no new GIMPLE opcodes, no alias facts from unproven ownership, no vectorizer changes outside justified facts, no "Crug must beat C" target (§26).
**Risk of skipping:** a false noalias is a silent miscompilation with no user-visible symptom until the program is wrong in production.

## Phase 12 — `c2crug` Migration Tool Integration
**Closes:** M12 — c2crug migration tooling.
**Prerequisite:** Phase 9 gate archived (metadata v1 stable) and Phase 5 gate archived; the migration corpus `migration/` with its own green test suite (its pinned commit is the commit at which this gate is archived) exists, and ADR 0023 fixes the proof rules, the corpus and the "largely preserved" reading.
**Exit gate:**
- `CHECK: scripts/check-c2crug.sh` => exit 0: the whole flow on `migration/` — `--analyze` reports ambiguous ownership and writes nothing, `--safe` copies the tree and applies five `own` edits, both reports validate, every transformed file compiles with `crugc -fcrug-ownership -fcrug-borrow -fcrug-ffi=libc.crugffi`, the migrated programs and the original corpus both print `migration: 8 programs pass` with identical output, and the report agrees with the output tree (5 own applied, 5 raw preserved).
- `CHECK: python3 scripts/validate-c2crug-report.py build/c2crug-check/safe.json` => exit 0: the safe-mode report is schema-valid (`crug-c2crug-report-v1`), every applied `own` carries a confidence and an evidence line, and no `raw` entry is applied.
- `CHECK: scripts/c2crug-diff.sh migration/ build/c2crug-check/out 20` => exit 0: at most 20 percent of the source lines changed (measured 10 of 222, 4.5 percent) and the comment-line count unchanged (16 -> 16), so comments and structure are preserved.
- `CHECK: mkdir -p build/c2crug-check && ./c2crug --analyze migration/ --report build/c2crug-check/analyze.json > build/c2crug-check/analyze.txt && grep -q "raw confidence" build/c2crug-check/analyze.txt && python3 scripts/validate-c2crug-report.py build/c2crug-check/analyze.json` => exit 0: ambiguous ownership appears as `raw` with a reason and an evidence line, the analyze report validates, and it claims no applied edit (no invented safety).
- `CHECK: make check-C2CRUG` => exit 0: `scripts/test-c2crug.py` passes its 32 checks (transform, confidence, raw preservation, analyze writes nothing, safe copies, a lying report rejected) and then the whole pipeline above runs again.
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (the tool introduces no diagnostic code; ADT-001..020 and the option-to-code map stay valid).
- `CHECK: make check-C_COMPAT` => exit 0 (the migration tool changes no compiler behaviour: plain C objects are unchanged).
**Non-goals:** no whole-program automatic migration, no borrow inference beyond suggestions, no crugc behaviour changes, no claim that migrated code is memory-safe (`no invented safety guarantees`, §17).
**Risk of skipping:** a tool that upgrades unproven C to "safe Crug" destroys the trust premise (§41) faster than any compiler bug.

## Phase 13 — Platform Abstraction Layer (PAL)
**Closes:** M13 — PAL stable.
**Prerequisite:** Phase 9 and Phase 5 gates archived (so `libcrug` builds against a real FFI/drop model); the PAL v1 contract ADR (`docs/crug/adr/0011-pal-contract.md`, `crug_pal.h`) accepted. The contract, both backends, the twelve-case suite and the lint exist at the commit this amendment records: the core compiles against `<crug_pal.h>` only, the fake backend exercises the suite with no operating system, and the Linux backend maps the header onto `pthread`/`futex`/`mmap`/`clock_nanosleep`.
**Exit gate:**
- `CHECK: make -C libcrug check-PAL PAL=fake` => exit 0 and the log ends `pal-contract[fake]: 12 cases, 0 failed, 1 skipped` (the one skip is the documented guard-page decline; the core compiles against the fake PAL and runs the whole suite).
- `CHECK: make -C libcrug check-PAL PAL=linux` => exit 0, the log ends `pal-contract[linux]: 12 cases, 0 failed, 0 skipped`, and `guard-fault: SIGSEGV on the guarded page as claimed` is present (the Linux backend passes the same suite and its guard pages really fault).
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => exit 0 and `lint-no-os-apis: 2 file(s) under libcrug/core, 0 operating-system primitive references` (the gate names `pthread_|epoll|futex|CreateThread|kqueue|WaitOnAddress`; the script also rejects the extended list).
- `CHECK: scripts/check-pal.sh` => exit 0 and `check-pal: fake and linux share 12 contract cases, 0 failures; fake backend links no system primitive, guard pages fault on linux` (same case names on both backends, no unexpected skip, no system symbol in the fake object).
- `CHECK: test "$(grep -R 'asm' libcrug/core | grep -vc '^$' || true)" = 0` => exit 0 (architecture-dependent context code lives only under `libcrug/arch/`, not `libcrug/pal/arch/` as the draft gate said).
- `CHECK: make check-C_COMPAT` => exit 0 (adding `libcrug` changes no compiler output).
- `CHECK: scripts/check-diagnostics.sh` => exit 0 and `111 registered code(s); 111 referenced code(s)` (Phase 13 adds no diagnostic but must not break the registry).
**Non-goals:** no scheduler, tasks or channels (P14–P16), no second OS port (P18), no pollers/io_uring (post-1.0, §32; the roadmap's poller work item is deferred by the project's own non-goal), no PAL ABI promise beyond v1.
**Risk of skipping:** OS calls leak into the core, every port becomes a scheduler fork, and `class T1` (RFC §7.6) becomes unprovable.

## Phase 14 — Goroutine / Task Runtime MVP
**Closes:** M14 — Goroutine runtime MVP.
**Prerequisite:** Phase 13 gate archived (PAL v1 frozen); ADR 0008 (goroutine stack model), ADR 0009 (scheduler model) and ADR 0013 (arch context switch policy) Accepted; `docs/crug/TASKS.md` and `docs/crug/GOROUTINES.md` written.
**Exit gate:**
- `CHECK: make check-GOROUTINES` => exit 0; prints `goroutines: 27 compiler test(s), 5 task program(s), 100000 tasks on 4 worker threads, 0 failure(s)` (spawn, yield, capture, owned capture and shutdown programs plus the dg cases).
- `CHECK: GOROUTINES_STRESS=1 ./build/tasks-stress` => exit 0; prints `tasks=100000 completed=100000 checksums=100000 lost=0 corrupt=0 workers=4 queue_peak=4`.
- `CHECK: test "$(CRUG_TASKS_WORKERS=4 GOROUTINES_STRESS=1 strace -f -e trace=clone,clone3 ./build/tasks-stress 2>&1 | grep -c clone)" = 4` => exit 0: 100000 tasks start 4 worker threads, not 100000.
- `CHECK: ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 make -C libcrug check-sanitizer` => exit 0.
- `CHECK: scripts/test-kernel-compat.sh && scripts/check-kernel-objects.sh out` => exit 0; prints `kernel-objects: 20 objects, __crug_=0, libcrug=0` (plain C and kernel builds still link no runtime).
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => exit 0; 0 operating-system primitive references: the scheduler reaches the OS only through the PAL.
- `CHECK: scripts/check-diagnostics.sh` => exit 0; every referenced code is registered, including CONC-008 and CONC-010 to CONC-014.
- `CHECK: make check-C_COMPAT` => exit 0; adding the runtime changes no compiler output.
**Non-goals:** no work stealing, no growable stacks, no preemption, no timers/fds, no structured scope (P15), no channels (P16), no Windows/macOS context (P18).
**Risk of skipping:** a "temporary" one-OS-thread-per-task design becomes the architecture, falsifying the M:N claim in §2.6 and `class T1`.

## Phase 15 — Structured Concurrency
**Closes:** M15 — Structured concurrency.
**Prerequisite:** Phase 14 gate archived (task runtime green); Phase 7 gate archived (borrow facts available for scoped captures); ADR 0024 accepted and `docs/crug/SCOPE.md` written.
**Exit gate:**
- `CHECK: make check-SCOPE` => exit 0, printing `scope: 23 compiler test(s), 8 group program(s), 0 leaked group(s), 0 failure(s)`. The eight group programs include scope-waits-for-all-children (`scope-wait: children=8 slots=8`, every slot filled before the block returns), the owned-capture transfer (`scope-owned: seen=S frees=1`, exactly one free), the scoped borrow (`scope-borrow: n=42`), early return (`scope-early: early=1 marks=2`) and the counters `entered == left` with `leaked task group state: 0`.
- `CHECK: crugc -fcrug-goroutines -fsyntax-only gcc/testsuite/crug.dg/scope/scope-goto-in.c` => exit 1 with `[crug-CONC-015]`: a control transfer cannot enter a scope block, because the group would never be entered or left.
- `CHECK: crugc -fcrug-goroutines -fcrug-ownership -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/scope/scope-borrow-lifetime.c` => exit 1 with `[crug-CONC-016]`: a scoped task may not borrow an object that dies before the scope ends, so a child never outlives its lender.
- `CHECK: ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 make -C libcrug check-sanitizer` => exit 0, ending with `tasks-sanitizer: ASan and UBSan clean over the suite, 100000 tasks and 200 scopes` and including `scope-stress: tasks=3000 entered=200 left=200 leaked task group state: 0` under LeakSanitizer.
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => exit 0: the group waits with `crug_pal_wait_u32`/`crug_task_yield`, never an operating-system primitive.
- `CHECK: scripts/check-diagnostics.sh` => exit 0: the two new codes `CONC-015` and `CONC-016` are registered and referenced.
- `CHECK: make check-C_COMPAT` => exit 0: the scope statement is a Crug-only contextual form and changes no plain-C translation.
**Non-goals:** no full cancellation trees or error aggregation beyond the baseline policy, no detached tasks, no task-local storage, no channels/select (P16).
**Risk of skipping:** unstructured tasks make every borrowed capture unsound and later turn channel shutdown into undefined lifetime territory.

## Phase 16 — Channels and `select`
**Closes:** M16 — Channels/select.
**Prerequisite:** Phase 15 gate archived; Phase 5 gate archived (owned-send exactly-once depends on deterministic drop); ADR 0025 accepted and `docs/crug/CHANNELS.md` written.
**Exit gate:**
- `CHECK: make check-CHANNELS` => exit 0, printing `channels: 40 compiler test(s), 7 channel program(s), 0 failure(s)`. The seven programs cover a buffered round trip (`channels-roundtrip: values=4 sum=10 closed=0`), a handoff over capacity 0, a `select` over two receive arms, a send arm, an owned transfer (`channels-owned: text=hi releases=1`, exactly one release), `close` (`channels-close: a=5 b=6 tail=0`) and fan-in (`channels-fanin: producers=5 total=20`).
- `CHECK: crugc -fcrug-channels -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/channels/chan-layout.c` => exit 0: `chan T` is one pointer whatever the element type is, including `chan style`/ `chan own char *`.
- `CHECK: crugc -fcrug-channels -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/channels/send-owned.c` => exit 1 with `[crug-CONC-003]`: a send transfers ownership, so an owning element must be wrapped in `move ()` and the receiver destroys it exactly once.
- `CHECK: crugc -fcrug-channels -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/channels/select-own-recv.c` => exit 1 with `[crug-CONC-017]`: a `select` arm cannot bind a receive from a channel with an owning element, because the arm may not run and the value would be lost.
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => exit 0: a blocked send or receive yields inside a task and calls `crug_pal_sleep_ns` on the main thread, never an operating-system primitive.
- `CHECK: scripts/check-diagnostics.sh` => exit 0: the new codes `CONC-017` to `CONC-023` are registered and referenced.
- `CHECK: make check-C_COMPAT` => exit 0: `chan`, `channel`, `send`, `recv`, `close` and `select` are Crug-only contextual words, so a plain-C translation is unchanged.
**Non-goals:** no fairness guarantee beyond the documented policy, no cancellation of blocked selects beyond the baseline, no channel `range`/iteration sugar, no lock-free performance claims (P21).
**Risk of skipping:** channel ownership duplicated or a waiter UAF surfaces as random memory corruption in user code, and the runtime loses credibility at Beta.

## Phase 17 — Send/Sync-Like Static Concurrency Safety
**Closes:** M17 — Send/Sync concurrency safety.
**Prerequisite:** Phase 16 gate archived (channels exist to carry values), Phase 9 gate archived (thread-affinity metadata fields), ADR 0026 accepted and `docs/crug/SEND-SYNC.md` written.
**Exit gate:**
- `CHECK: make check-SEND_SYNC` => exit 0 printing `send-sync: 14 compiler test(s), 0 failure(s); raw pointer rejected, thread affinity enforced, FFI annotations enforced` (14 expected passes; the script also runs the unchanged `spawn` program, the raw-pointer rejection, both thread-affinity cases and both FFI annotation cases).
- `CHECK: crugc -fcrug-goroutines -fsyntax-only gcc/testsuite/crug.dg/send-sync/raw-ptr.c` => exit 1 with `[crug-CONC-024]`: a raw pointer is not `Send`, and the documented rule is rejection, not a warning.
- `CHECK: crugc -fcrug-goroutines -fcrug-ownership -fsyntax-only gcc/testsuite/crug.dg/send-sync/send-own.c` => exit 0: the positive twin, an owned pointer moved into a task, is still accepted.
- `CHECK: crugc -fcrug-goroutines -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/send-sync/sync-ptr.c` => exit 1 with `[crug-CONC-026]`: a shared borrow of a pointer-holding struct is not `Sync`.
- `CHECK: crugc -fcrug-channels -fsyntax-only gcc/testsuite/crug.dg/send-sync/chan-ptr.c` => exit 1 with `[crug-CONC-027]`: a channel element type must be `Send`.
- `CHECK: scripts/check-diagnostics.sh` => exit 0: the new codes `CONC-024` to `CONC-027` are registered and referenced.
- `CHECK: make check-C_COMPAT` => exit 0: the property is checked at compile time only, so a plain-C translation unit is unchanged.
**Non-goals:** no Rust-equivalent auto-trait inference beyond the documented inference, no dynamic checks, no runtime lock injection, no kernel-mode concurrency checking.
**Risk of skipping:** without static Send/Sync the channel and `go` features ship a data-race generator that the ownership model appears to bless.

## Phase 18 — Multi-OS Runtime
**Closes:** M18 — Multi-OS runtime.
**Prerequisite:** Phase 14–16 gates archived on Linux; ADR 0012 (operating-system and architecture backend policy) and `docs/crug/PORTABILITY.md`; the CI runners for windows-x86_64 and macos-arm64 exist (`.github/workflows/multi-os.yml`), whose runs are green (run 39355413321 at `f086068`, four jobs); the owner still has to add them to the required checks; the port evidence needs either the host cross toolchains (`aarch64-linux-gnu-gcc` + `qemu-aarch64`, `x86_64-w64-mingw32-gcc` + `wine`) or the container images `crug-arm64-build` and `crug-wine`.
**Exit gate:**
- `CHECK: make check-MULTI_OS` => exit 0 on this host: one `make` target runs the twelve-case PAL contract, the task suite, the stress run and three plain-C channel programs, and prints `multi-os: <os>/<arch> pal-contract 12 cases, tasks 17 checks, 3 channel program(s)`.
- `CHECK: scripts/check-multi-os-ports.sh` => exit 0: the Linux AArch64 port and the Windows x86-64 port are built and executed (AArch64 under qemu in `crug-arm64-build`, Windows cross-built with `x86_64-w64-mingw32-gcc` and run under Wine), each asserting `tasks: 17 checks, 0 failed` at 2, 4 and 8 workers plus the three channel lines; the script names every skipped port and exits 2 when no port could run, so a silent pass is impossible.
- `CHECK: test "$(grep -RE '#if.*(_WIN32|__APPLE__|__linux__)' libcrug/core | wc -l)" -eq 0` => exit 0: no platform ifdef in the scheduler core; every platform name lives under `libcrug/pal/` or `libcrug/arch/` (ADR 0012 D2).
- `CHECK: make -C libcrug check-PAL PAL=fake` => exit 0: the same twelve-case contract passes with no operating system at all, so the contract, not the backend, is what the suite tests.
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => exit 0: no operating-system primitive is called from the core.
- `CHECK: make check-C_COMPAT` => exit 0: the runtime is a library, so a plain-C translation unit is unchanged.
**Non-goals:** no BSD before 1.0 (policy decision, §23), no IOCP/io_uring/kqueue advanced backends (master-spec P19, post-1.0), no RISC-V, no cross-OS binary compatibility.
**Risk of skipping:** the §2.7 portability promise is unverified, and the first Windows user discovers that the "portable" core is a Linux scheduler.

## Phase 19 — Linux Kernel Build Qualification
**Closes:** M19 — Linux kernel build milestone.
**Prerequisite:** Phase 2 gate archived (stage K0 microtests green in mandatory CI) plus the pinned kernel source of `docs/crug/KERNEL-BASELINE.md`, extracted under `build/linux-6.18.57` with its digest verified, and the object corpus of `kernel/KERNEL-CORPUS.md`. Stage K6 needs `qemu-system-x86_64`; stage K7 needs `aarch64-linux-gnu` binutils and builds a GCC 16.2.0 cross compiler from this tree on first use. Run the phase as `scripts/gate.sh --run phase-19 --timeout 3600`: the full build and the first K7 cross build exceed the gate driver's default 120 second limit.
**Exit gate:**
- `CHECK: for s in K1 K2 K3 K4 K5 K6 K7; do ./scripts/kernel-stage.sh $s || exit 1; done` => exit 0, with one pass line per stage: `K1: 8 header probe(s) compiled`, `K2: 12 translation unit(s) compiled`, `K3: lib/ built`, `K4: 438 corpus object(s) compiled`, `K5: full build ok`, `K6: boot smoke ok`, `K7: 15 aarch64 object(s) compiled`.
- `CHECK: test -s build/kernel-crug/vmlinux && test -s build/kernel-crug/arch/x86/boot/bzImage && test -s build/kernel-crug/System.map` => exit 0: the full pinned build produced the kernel image and its map.
- `CHECK: test "$(nm build/kernel-crug/vmlinux | grep -c '__crug_')" -eq 0` => exit 0: no mandatory runtime symbol, and no libcrug dependency, in the built kernel.
- `CHECK: ./scripts/kernel-stage.sh K6` => exit 0, printing `K6: boot smoke ok: 6.18.57 banner, /init ran, crug-boot-ok once, clean poweroff` (qemu booted the K5 kernel, ran the freestanding `/init`, saw the marker exactly once and exited on poweroff).
- `CHECK: ./scripts/kernel-object-diff.sh` => exit 0, printing `438 object(s): 437 identical, 1 explained, 0 unexplained`: every delta against the same corpus built by the wrapped GCC driver is absent or has a line in `kernel/object-diff-allow.txt` (the one explained object embeds the build timestamp).
- `CHECK: ./scripts/check-p0-open.sh --kernel` => exit 0, printing `check-p0-open: 0 open P0 in scope 'kernel' (0 fixed P0/P1 without a regression id)`.
**Non-goals:** no kernel ownership contracts (master-spec 8K), no RCU-aware modeling, no kernel task backend, no second-architecture boot (stage K7 compiles and validates a subset only).
**Risk of skipping:** full build and boot are the only evidence that the syntax-compatibility baseline generalizes; without them the kernel claim is a microtest story.
**Sizes (formerly a `VAGUE` item):** `kernel/KERNEL-CORPUS.md` pins the sizes: stage K3 is `lib/` as a whole (106 objects), stage K4 is 438 x86_64 objects in nine directories, and stage K7 is 15 aarch64 objects. The lists are generated from the pinned configuration and only change by amendment (A-22).

## Phase 20 — Diagnostics, Tooling, Developer Experience
**Closes:** M20 — Tooling/diagnostics hardening.
**Prerequisite:** gates M11, M18 and M19 archived (§33 dependency graph); a stable diagnostic-code registry exists; `gcc/crugc` builds; `python3` is present for the SARIF check. The corpus is the published list at `gcc/testsuite/crug.dg/diagnostics/top50.txt` with one fixture and one golden per class; the documentation examples live in `docs/crug/MIGRATION-EXAMPLES.md`.
**Exit gate:**
- `CHECK: scripts/check-diagnostics.sh` => exit 0.
- `CHECK: test "$(grep -vc '^[[:space:]]*\(#\|$\)' gcc/testsuite/crug.dg/diagnostics/top50.txt)" -eq 50` => exit 0 (50 published classes; `make check-DIAGNOSTICS` proves each has a fixture and a golden).
- `CHECK: make check-DIAGNOSTICS` => exit 0, printing `check-diagnostic-goldens: 50 case(s) match their golden, 0 drifted` and `check-diagnostic-sarif: ok`.
- `CHECK: scripts/check-diagnostic-sarif.sh` => exit 0, printing `sarif: ruleId crug-UAM-001, origin note present, rule documented` (machine-readable: the code is the SARIF `ruleId`, the origin note is a result).
- `CHECK: test "$(grep -c 'GIMPLE' docs/crug/DIAGNOSTICS.md)" -eq 0` => exit 0 (no user-facing fix instruction names a compiler dump).
- `CHECK: make check-docs-examples` => exit 0 (the examples quoted in the migration guide compile and run).
**Non-goals:** no full LSP server, no formatter implementation (decision only), no debugger front-end, no syntax-highlighting grammar completeness beyond the decision record.
**Risk of skipping:** safety features nobody can act on are turned off project-wide, which negates P4–P11 in practice.
**Sizes (formerly a `VAGUE:` item):** "top 50 common errors" is defined as exactly 50 rows in `top50.txt`, one distinct registered code and one distinct fixture per row, restricted to areas the compiler can emit today (GRAMMAR and KERNEL have registered codes but no emitted diagnostic, so they are documented as excluded); the owner ratifies the list in the same review as the phase.

## Phase 21 — Performance Qualification
**Closes:** M21 — Performance qualification.
**Prerequisite:** Phases 18, 19 and 20 gates archived; the harness under `bench/`; the recorded plain-C baseline `bench/baseline.json`; the unmodified GCC 16.2 compiler built by `make pristine-xgcc`; the 438-object kernel corpus of `kernel/KERNEL-CORPUS.md`; `python3`. Every number below is ratified in `docs/crug/RELEASE-POLICY.md` section 2, and the gate needs `scripts/gate.sh --run phase-21 --timeout 1200` because the pristine, plain-C and concurrency checks rebuild and re-time many programs.
**Exit gate:**
- `CHECK: bench/run.py --zero-cost --check` => exit 0, printing `zero-cost: 12 comparison(s), 0 added instruction(s), 0 added call(s)` and `zero-cost: OK` (RELEASE-POLICY 2.1).
- `CHECK: bench/run.py --pristine --check` => exit 0, printing `pristine: 114 source/option pair(s), 0 object difference(s), 0 LTO timing violation(s)` and `pristine: OK` (2.2).
- `CHECK: bench/run.py --plain-c --check` => exit 0, printing `plain-c: OK` after every non-LTO object matched the recorded baseline (2.3).
- `CHECK: bench/run.py --concurrency --check` => exit 0, printing the dispatch line with the Crug/pthread ratio at most 1.5, the memory line with the Crug figure at most half the pthread figure, the channels line, and `concurrency: OK` (2.4 and 2.5).
- `CHECK: scripts/check-kernel-perf.sh` => exit 0, printing `kernel-perf: 438 object(s): 437 identical, 1 explained, 0 differing` (2.6).
- `CHECK: make check-PERFORMANCE` => exit 0, printing `performance: OK`; it runs the five checks above and writes `bench/results/<date>.md` with a pass verdict (2.7).
**Non-goals:** no "Crug must beat C" target (§26), no micro-optimization of the compiler itself, no new optimizations introduced here (only measurement and sign-off), no kernel codegen rework, no stack pooling or scheduler redesign (the pool ratio is recorded as evidence for future work only).
**Risk of skipping:** unmeasured zero-cost claims are marketing, and §2.8 ("no hidden runtime overhead") stays unprovable at release.
**Sizes (formerly a `VAGUE:` item):** the zero-cost matrix is exactly four cases at three optimisation levels (12 comparisons); the pristine check is 114 source/option pairs; the kernel corpus is 438 objects; each dispatch arm runs 200 tasks and each timing is the minimum of three runs; the thresholds are ratified in `RELEASE-POLICY.md` section 2 (amendment A-1: dispatch compares pthread-per-task, and the allocation-free pool ratio is recorded only).

## Phase 22 — Security and Reliability Qualification
**Closes:** M22 — Security/reliability qualification.
**Prerequisite:** Phase 21 gate archived; the six harnesses under `fuzz/` (parser, ownership, borrow, `.crugffi`, scheduler, channel state machine) and their committed seed corpora exist; `scripts/check-sanitizer.sh`, `scripts/check-regressions.sh` and `scripts/check-fuzz-nightly.sh` exist; `.github/workflows/fuzz-nightly.yml` is on `main` and has been scheduled; `clang` with libFuzzer, `gcc` and `python3` are available; the budgets and the nightly window are ratified in `docs/crug/RELEASE-POLICY.md` section 3 (`scripts/gate.sh --run phase-22 --timeout 1800`).
**Exit gate:**
- `CHECK: make check-FUZZ` => exit 0, printing `fuzz: 6 harness(es), 0 crash(es), 0 timeout(s), 0 new finding(s)` and `fuzz: OK` (four compiler targets plus the scheduler and channel-state-machine harnesses; default 300 inputs per compiler target, runtime defaults per RELEASE-POLICY 3.1).
- `CHECK: make check-SANITIZER` => exit 0, printing `sanitizer: ASan and UBSan clean over tasks/scopes/channels; TSan clean on the OS-thread channel arm; fiber exception recorded in ADR 0028` (arm 1 is `scripts/check-tasks-sanitizer.sh`, arm 2 is ASan+UBSan over the channel suite and an OS-thread channel+PAL stress program, arm 3 is clang TSan over the same stress with no fiber switch; the exception is `docs/crug/adr/0028-tsan-fiber-exception.md`, per RELEASE-POLICY 3.2).
- `CHECK: ./scripts/check-p0-open.sh && ./scripts/check-regressions.sh` => exit 0, printing `check-p0-open: 0 open P0 (0 fixed P0/P1 without a regression id)` and `check-regressions: <n> fixed P0/P1 entr(ies), 0 without a regression test` (every fixed P0/P1 entry in `docs/crug/KNOWN-ISSUES.md` names an existing regression path, per RELEASE-POLICY 3.4; `<n>` is the number of fixed P0/P1 entries, two today: K-001 and K-002).
- `CHECK: ./scripts/check-fuzz-nightly.sh` => exit 0, printing `fuzz-nightly: 30 consecutive scheduled success(es), window 30` (RELEASE-POLICY 3.3; exit 1 means the window is not met and exit 2 means the data is unavailable, and neither is evidence of success).
**Non-goals:** no formal verification, no proof of memory safety inside `unsafe`, no third-party audit unless ADR'd, no new language or runtime features during hardening.
**Risk of skipping:** an RC with a latent P0 miscompilation is the worst failure mode in §3 and cannot be patched out of a 1.0 tag.
**Window (formerly a `VAGUE:` item):** the qualification window is 30 consecutive successful `schedule` runs of `.github/workflows/fuzz-nightly.yml`, ratified in `docs/crug/RELEASE-POLICY.md` section 3.3. A push-triggered run does not count, a failed or in-progress scheduled run resets the window, and an API that cannot be read is exit 2 from the checker rather than a pass. Because the window is calendar time, this gate stays red until the runs accumulate; it must not be reported green on a shorter window.

## Phase 23 — Documentation and Compatibility Policy
**Closes:** no milestone of its own — CONFLICT, see C1. Roadmap §28 labels it "Part of M23"; §30 assigns M23 to Phase 25. Adjudication: contributes to M23, closed only when Phase 25 closes M23.
**Prerequisite:** Phase 4–P19 gates archived at the same commit (a document cannot be frozen before the feature it describes); `docs/crug/` tree and ADR directory exist.
**Exit gate:**
- `CHECK: for f in LANGUAGE GRAMMAR OWNERSHIP BORROWING UNSAFE C-FFI ABI KERNEL-COMPATIBILITY FREESTANDING CONCURRENCY CHANNELS PORTABILITY PAL OPTIMIZATION TESTING COMPILER-INTERNALS MIGRATION RELEASE-POLICY COMPATIBILITY-POLICY; do test -s docs/crug/$f.md; done` => exit 0 for all 19 (§28).
- `CHECK: scripts/check-docs.sh` => every document has the sections `REQUIRED-DOC-SECTIONS.md` names, and no document promises a §32 post-1.0 feature as present.
- `CHECK: for k in 'language versioning' 'Crug ABI versioning' '.crugffi versioning' 'libcrug ABI policy' 'feature deprecation' 'GCC baseline policy' 'supported platforms' 'supported C dialects' 'kernel support policy'; do grep -qi "$k" docs/crug/COMPATIBILITY-POLICY.md; done` => exit 0 (all nine §28 items defined).
**Non-goals:** no normative spec changes or new RFCs, no language reference beyond the implemented surface, no documentation of post-1.0 features as commitments.
**Risk of skipping:** without a written compatibility policy every future release is an ad-hoc judgment call and the GCC-baseline promise erodes silently.

## Phase 24 — Packaging and Distribution
**Closes:** no milestone of its own — CONFLICT, see C2. Roadmap §29 assigns none and §33's graph jumps M23→M24. Adjudication: contributes to M23; M23 cannot be declared until this gate is green.
**Prerequisite:** Phase 18 and Phase 21 gates archived (packages per supported platform, plus the reproducibility and performance baselines), and a checksum/SBOM process ADR.
**Exit gate:**
- `CHECK: docker run --rm crug/pkg:<version> crugc --version` => exit 0 and prints compiler, language and runtime ABI versions separately (§8.2 of RFC 0001).
- `CHECK: for t in hello.c hello-own.c runtime-sample.c; do docker run --rm -v "$PWD:/w" -w /w crug/pkg:<version> crugc $t -o /tmp/x && /tmp/x; done` => exit 0 for all three (§29 acceptance).
- `CHECK: sha256sum -c SHA256SUMS` => every artifact `OK`; `test -s crug-<version>.spdx.json` and license notices present.
- `CHECK: test -f docs/crug/BUILD-INSTRUCTIONS.md && grep -q '<version>' CHANGELOG.md` => exit 0 (build instructions and complete version information shipped).
**Non-goals:** no apt/rpm/Homebrew/Chocolatey/winget channels (explicitly optional, §29), no cross-compiler packages, no installer UI, no container images beyond the CI/build image.
**Risk of skipping:** a compiler nobody can install reproducibly cannot be independently validated, so the trust claim rests on the authors' machines.

## Phase 25 — Release Candidate
**Closes:** M23 — Crug 1.0 Release Candidate. CONFLICT: §28 also attaches M23 to Phase 23; see C1 — M23 is closed here only, with Phase 23 and Phase 24 gates as preconditions.
**Prerequisite:** Phase 0–24 gates archived at the same commit; documentation frozen at a signed tag; the owner-ratified P1 threshold written into `docs/crug/RELEASE-POLICY.md`.
**Exit gate:**
- `CHECK: scripts/gate.sh --all` => every phase archive for 0–24 present at HEAD's commit and exit 0 (§30 "all prior milestone blockers closed").
- `CHECK: ./scripts/check-p0-open.sh && ./scripts/count-open-p1.sh --max=<N>` => 0 P0 and open P1 ≤ N from `RELEASE-POLICY.md`.
- `CHECK: ./scripts/kernel-stage.sh K5 && ./scripts/kernel-stage.sh K6` => full primary kernel build and boot smoke green at the RC commit.
- `CHECK: make check-MULTI_OS && make check-FFI && make check-PERFORMANCE` => exit 0 on Linux, Windows and macOS; performance sign-off artifact present.
- `CHECK: git diff <rc-tag>..HEAD -- gcc/crug/ gcc/crug/parser* gcc/crug/grammar*` => empty during RC (bug fixes only, no syntax additions).
**Non-goals:** no architecture changes, no new syntax or features, no new platforms, no benchmark-target changes, no documentation redesign — bug fixes only (§30 RC rules).
**Risk of skipping:** an unfrozen RC is a beta with a version number, and the 1.0 tag inherits every unfinished argument.
`VAGUE:` "agreed P1 threshold" is not a number in the roadmap; it must be ratified by the owner before this phase opens, otherwise the gate cannot be adjudicated and stays open.

## Phase 26 — Crug 1.0
**Closes:** M24 — Crug 1.0.
**Prerequisite:** Phase 25 gate archived at the tagged commit, with 0 P0 open for the ratified qualification window; conformance-matrix draft exists in `docs/crug/CONFORMANCE-MATRIX.md`.
**Exit gate:**
- `CHECK: scripts/check-production-ready.sh spec/CRUG_PRODUCTION_ROADMAP.md` => exit 0: every §2 checklist item is both checked and backed by a gate archive, 0 unchecked, 0 unchecked-without-evidence.
- `CHECK: scripts/check-release-claims.sh` => every claim in the §31 release statement maps to a passing gate (no orphan claims, no unsupported sentence).
- `CHECK: test -x crugc && test -d libcrug && test -f docs/crug/COMPATIBILITY-POLICY.md && test -f crug-1.0.notes.md` => exit 0 (all §31 release artifacts present, c2crug release compatible).
- `CHECK: git verify-tag crug-1.0` => signed tag verifies, and `scripts/check-conformance.sh --class P1` => exit 0 (RFC 0001 §7.8 matrix satisfied).
**Non-goals:** generics, traits, shared ownership, RCU-aware modeling, kernel task backend, io_uring, growable stacks, extra BSDs, RISC-V, embedded profiles, GPU/AI memory spaces, enhanced c2crug, LSP, formatter and package ecosystem (§32) — none may be a prerequisite for 1.0.
**Risk of skipping:** 1.0 becomes a feature count instead of the trust statement in §41, which is the one thing the roadmap says the project optimizes for.

---

## 8. Conformance class coverage (index only; adds no requirement)

This section maps the RFC 0001 §7 classes to the gates above. A class may be
claimed only when every listed gate archive exists at the tagged commit. The
mapping is derived from where the required behaviour is actually tested; it does
not add checks.

| Class | Required behaviour (RFC 0001) | Gates that must be green |
|-------|-------------------------------|--------------------------|
| C0 | preserve contextual words as identifiers where C allows | Phase 1, 3, plus the flags-off regression check in every later phase |
| K0 | no hidden allocation, TLS, locks, scheduler state or bounds checks in plain C | Phase 2, 19 |
| O1 | reject implicit owned copy; deterministic drop for supported control flow | Phase 4, 5 (see ADR 0004, ADR 0005) |
| B1 | reject move/drop while an incompatible borrow is live | Phase 6, 7 |
| F1 | reject incompatible or malformed safety metadata deterministically | Phase 9 |
| T1 | must not require one OS thread per task | Phase 13, 14 (100k-task check) |
| C1 | release-defined synchronization ordering | Phase 15, 16, 17 |
| P1 | the 1.0 production matrix | Phase 25, 26, and RFC-0010 |

**P1 is unclaimable today.** RFC 0001 §7.8 defines P1 by "the 1.0 production matrix
published by the release RFC", and no such RFC exists. The Phase 26 check that runs
`check-conformance.sh --class P1` therefore cannot pass until RFC-0010 is written,
accepted and published. That is conflict C-05 in spec/000-PRECEDENCE.md. If the 1.0
release does not claim P1, Phase 26 must state which of C0/K0/O1/B1/F1/T1/C1 are
claimed and archive only those.

## 9. Release definitions

The roadmap §38 defines Prototype, Alpha, Beta and Production, but Alpha is
described only by comparison with Prototype and the §37 matrix has non-boolean
cells. Until the owner ratifies numbers, this file treats a release level as
claimable only when:

- **Alpha:** Phase 5 gate archived (drop is exact), Phase 6 gate archived (lexical
  borrow), Phase 19 boot smoke green, zero open P0.
- **Beta:** Phases 5-19 gates archived, zero open P0 and zero open P1, Phase 21
  thresholds met.
- **1.0:** Phase 25 and 26 gates archived, zero open P0/P1, conformance archives
  present for every claimed class (section 8).

These are gate-derived readings of §38, not new requirements. The owner should
replace them with explicit numbers in `docs/crug/RELEASE-POLICY.md` before Phase 25
opens.

## 10. Related decisions

- ADR 0002 fixes which contextual words have Crug meaning (Phase 3 gate).
- ADR 0003 fixes the ownership metadata storage model and the C-ABI erasure rules (Phase 4 gate).
- ADR 0004 fixes aggregate ownership analysis, drop order and adoption (Phase 4 gate).
- ADR 0005 fixes destructor attachment and control-flow cleanup (Phase 5 gate).
- spec/000-PRECEDENCE.md decides conflicts between documents. Conflicts C1 and C2
  above use a different numbering from that file's C-01..C-11 register and should
  be cross-referenced when the register is next edited.

## 11. Known weaknesses of this gate set

Recorded at the commit that introduces this file. Each item is a gate that could
pass without proving what it claims, and each names who must fix it.

1. **Phase 22 (security/reliability) cannot be adjudicated from the tree.** Its
   evidence is CI run history over a time window ("30 consecutive green days") when
   the rule requires archived evidence at a commit. Fix: replace the window with
   archived fuzz and sanitizer artefacts, or amend the rule in section 2.4.
2. **Phase 24 (packaging) is self-referential.** `sha256sum -c SHA256SUMS` verifies
   digests generated from the artefacts it checks, and `docker run crug/pkg:<version>`
   presumes a published image. Fix: add a rebuild-from-tag check that produces the
   artefacts and compares them byte-for-byte.
3. **Phase 23 (documentation) accepts stubs.** `test -s` plus phrase greps pass on a
   one-line document. Fix: cross-check each documented feature against a named test
   directory, as Phase 20 does for diagnostics.
4. **Phase 17 (Send/Sync) has a CHECK that cannot fail.** Its second condition
   accepts either "rejected" or "accepted with the documented warning". Fix: require
   one behaviour and add a stress/sanitizer check behind the data-race claim.
5. **Phase 19 (kernel) leaves corpus size to human judgement.** "Every object
   difference has a recorded explanation" is not mechanical, and the VAGUE marker on
   stages K3/K4 lets the pinned file list shrink. Fix: freeze the list in
   `kernel/KERNEL-CORPUS.md` with counts before M19 is claimed.

The same pattern may exist in other VAGUE markers (7 in this file). A VAGUE marker
is an open gate by section 2.5; this list is the subset known to be exploitable.

## 12. Amendment record

| # | Date | Change | Why |
|---|------|--------|-----|
| A-1 | 2026-10-19 | Status changed from Proposed to Accepted; C-06 settled by section 7 (phase-to-milestone mapping is authoritative as written there). | The owner ratified the gate set. Phases 0 and 1 were adjudicated before ratification; their evidence is archived at the commits recorded in each gate directory. |
| A-2 | 2026-10-19 | Phase 2 checks 2-4 replaced by scripted observables: `scripts/check-kernel-objects.sh out`, `scripts/check-kernel-freestanding.sh`, `scripts/check-ci-gate.sh`. The prerequisite now names `docs/crug/KERNEL-COMPATIBILITY.md` and `make check-KERNEL_COMPAT`. | The original check 2 was a shell loop with no `out/` provenance and no exit status, check 3 described prose observables of a compile that produced no named object, and check 4 embedded a `gh api` pipeline with placeholders (`<owner>/<repo>`, `HEAD`) that could not be run verbatim. Each is now one command with one printed observable that `scripts/gate.sh` can adjudicate. |
| A-3 | 2026-10-19 | Phase 9 checks 4-5 now spell the sidecar flag as `-fcrug-ffi=<path>` and give check 4 a C input file. | ADR 0007 defines `-fcrug-ffi=<path>` (repeatable) and forbids a bare `-fcrug-ffi` from meaning "discover everything"; the old check passed the `.crugffi` file to `-fsyntax-only` as if it were C and the diff-obj arm passed `-fcrug-ffi` with no file. Both are now commands that run verbatim. |
| A-4 | 2026-10-19 | Phase 3 checks 1-4 rewritten as four runnable commands; the acceptance arm moved into `scripts/check-crug-flags.sh` and the GNU C arm into `scripts/check-gnu-c-compat.sh`. | Check 1 compiled a file named `t.c` that the gate never creates, and mixed acceptance with the unknown-option rejection; check 3 said "under every flag combination" as prose and used an inline program that the floor cannot reproduce from a clean checkout; check 4 named a Make target that did not exist. The scripts now assert the same facts, including the macro closure and the kernel conflicts, and print one countable line each. |
| A-5 | 2026-10-19 | Phase 4 check 1 greps the object for `__crug_` instead of the word `own`; check 2 compiles `move-zero-cost.c` before disassembling it; check 4 is split into exit-status and diagnostic-code checks; check 6 spells the dump flag `-fcrug-dump=ownership` and greps the emitted file. | Check 1 counted the `takes_own`/`returns_own` symbol names in the test, which is 1, not 0, while the property it wants is the absence of generated `__crug_` metadata. Check 2 disassembled an object no earlier check produced. Check 4's observable (exit 1 *and* a code in stderr) could not be evaluated by `scripts/gate.sh`, which adjudicates one exit status. Check 6 named an option that does not exist; the accepted spelling is `-fcrug-dump=<kind>`. |
| A-6 | 2026-10-19 | Phase 5 checks 1-5 replaced by six scripts (`check-drop-suite.sh`, `check-drop-exactly-once.sh`, `check-drop-asan.sh`, `check-drop-objdump.sh`, `check-drop-policy.sh`, `check-drop-dump.sh`); `scripts/check-diagnostics.sh` added as check 7; the prerequisite now names `DROP.md`, ADR 0005 A-1..A-7 and the destructor decision. | The old checks named a Make target that did not exist (`check-DROP-asan`), a directory that is not a command (`gcc/testsuite/crug.dg/drop/exactly-once`), an `objdump` over object files no check produced (`drop/*.o`), and one `-fsyntax-only` run where the gate needs exit status *and* a diagnostic code. Prints of the form `allocs=1 frees=1` were prose about a program the floor could not reproduce. Each item is now a script that prints one countable line and exits nonzero on the property it asserts. |
| A-7 | 2026-10-19 | Phase 6 checks replaced by five scripts (`check-borrow-suite.sh`, `check-borrow-cost.sh`, `check-borrow-abi.sh`, `check-borrow-policy.sh`, `check-borrow-dump.sh`) plus one compile-fail exit status and `scripts/check-diagnostics.sh`; the prerequisite now names `BORROW.md`. | The old check 1 named the suite by a Make target whose observable (a list of accepted and rejected *shapes*) `scripts/gate.sh` cannot adjudicate; check 2 disassembled `borrow-cost.o`, which no check produced; check 3 counted `pthread_mutex`/`__crug_lock`, which the phase never emits, so it could not fail for the reason it names; check 4 required exit 1 *and* a code in stderr in one line. Each item is now one command with one exit status and, where useful, one countable printed line. |
| A-8 | 2026-10-19 | Phase 7 checks replaced by six scripts (`check-borrow-suite.sh`, `check-borrow-real-c.sh`, `check-borrow-escape.sh`, `check-borrow-nll.sh`, `check-borrow-policy.sh`, `check-diagnostics.sh`) plus the two compatibility suites; the prerequisite now names ADR 0019 and ADR 0020 and the `-fcrug-dump=borrow` spelling. | The old check 1 named the suite by a target and described accepted shapes in prose; check 2's `grep -c 'internal compiler error'` exits 1 on zero matches, so a clean corpus failed the gate, and its two-level `real-c/*/*.c` path did not exist yet; check 3 required exit 1 *and* two quoted locations in one line; check 4 was a Make invocation whose two arms the floor cannot attribute. The old prerequisite cited an option that does not exist (`-fdump-crug=borrow`) and a `VAGUE` note asked the owner to name the summary depth, which ADR 0019 now fixes at zero. |
| A-9 | 2026-10-19 | Phase 8 checks replaced by four scripts (`check-strict-raw.sh`, `check-strict-codegen.sh`, `check-strict-suite.sh`, `check-strict-policy.sh`) plus the two compatibility suites and `scripts/check-diagnostics.sh`; the prerequisite now names ADR 0021 and `STRICT.md`. | The old check 1 packed two observables (exit 1 with a code, and exit 0 without the flag) into one line; check 2 ran `diff-obj` over a directory that did not exist and compared `-O2` against `-O2 -fcrug-strict`, which cannot compile a fixture that uses `raw` or `unsafe` at all; check 3 named prose cases ("a `longjmp` case asserting the documented policy, or the ADR's alternative") that `scripts/gate.sh` cannot adjudicate and referred to an ADR that did not exist yet. Each item is now one command with one exit status and one countable printed line. |
| A-10 | 2026-10-19 | Phase 9 checks replaced by six runnable commands (`make check-FFI`, `scripts/check-ffi-integration.sh`, `scripts/check-ffi-conflict.sh`, `scripts/check-ffi-codegen.sh`, `scripts/check-ffi-policy.sh`, `scripts/check-diagnostics.sh`); the prerequisite now names `FFI.md` and the fixture directories. | Check 2 hashed `lib/libsqlite3.so.0` and three siblings: no `lib/` directory exists, and the host's runtime libraries live in `/usr/lib/x86_64-linux-gnu/`, so the command could not run at all. Check 3 named `gcc/testsuite/crug.dg/ffi/integration-sqlite` as if it were a program; nothing built it, and its observable (`allocs=1 frees=1`) was prose about unnamed code. Check 4 passed a `.crugffi` file to `-fsyntax-only` as if it were C and needed an input file instead; check 5's `diff-obj` arm named `-fcrug-ffi=` with a file whose declarations the corpus did not declare. Each item is now one command with one exit status and one countable printed line, and the four-library claim is asserted by hashing the real sonames before and after. |
| A-11 | 2026-10-19 | Phase 10 checks replaced by seven runnable commands (`make check-SAFE_TYPES`, `scripts/check-safe-types.sh`, `scripts/check-safe-bounds.sh`, the layout compile, the non-exhaustive `match` compile, `make check-C_COMPAT`, `scripts/check-diagnostics.sh`); 10D payload enums and owned payloads recorded as a `VAGUE` item with the `adts-drop` command increment 2 must produce, so M10 cannot be awarded yet. | Old check 1 lumped 10A-10D into one Make observable; check 2 compiled `layout.c` without the flags that make the generic types parse, so it could only fail; check 3 named `gcc/testsuite/crug.dg/safe-types/adts-drop` as if a directory were a command, and no such test can exist before owned payloads; check 4 named `non-exhaustive-match.c`, which the suite spells `match-non-exhaustive.c`, and required exit 1 *and* a diagnostic in one line that `scripts/gate.sh` cannot adjudicate; check 5 alone did not exercise the new diagnostics. Increment 1 (2026-10-19) implements 10A, 10B, 10C, `match` and checked indexing; 10D stays open by section 2.5. |
| A-12 | 2026-10-19 | Phase 10 gate extended by increment 2a: check 1's floor raised from 32 to 55 expected passes, the `layout.c` check scoped to `slice`/`option`/`result`, and two checks added (`payload-enum-layout.c` compiles, `payload-enum-forward.c` fails with `[crug-ADT-017]`); the `VAGUE` item now covers owned payloads only. | Increment 2a implements 10D payload enums and `match` over them, so the old `VAGUE` text ("a payload enum needs a record storage type registered under an enum tag") is no longer true: `lookup_tag`/`parser_xref_tag` accept the pairing and the payload-enum `ABI.md` row is frozen. The gate now asserts that layout and the forward-reference rule, and keeps only the owned-payload `adts-drop` command open. |
| A-13 | 2026-10-19 | Phase 10 gate closed: check 1's floor raised from 55 to 77 expected passes, `scripts/check-owned-payloads.sh` added as a check, the diagnostic check extended to ADT-018..ADT-020, and the owned-payload `VAGUE` item removed. | Increment 2b implements owned payloads, so the observable A-11 and A-12 kept open now exists and passes: one drop for the active variant, none for an inactive one, a tag guard in the generated code, and the six unsafe forms rejected. Nothing in the gate is prose any more, so M10 is adjudicable by `scripts/gate.sh --run phase-10`. |
| A-14 | 2026-10-19 | Phase 11 checks replaced by seven runnable commands (matrix check, `scripts/check-optimization.sh`, `make check-OPTIMIZATION`, plain-C check, `scripts/check-stop-conditions.sh`, `scripts/check-diagnostics.sh`, `make check-C_COMPAT`); byte identity of the Crug and hand-written `restrict` kernel objects is the primary observable, with the 5% band measured only when the objects differ; the LTO timing band is replaced by LTO correctness; the `VAGUE` marker on "restrict-like optimization" is removed because ADR 0010 fixes the number. | Check 1's timing band cannot adjudicate this host: repeated runs of two *identical* objects were observed between 0.65 and 2.3, so a 1.05 ratio would fail at random. Check 2 diffed `-fdump-tree-alias` output, which is a points-to dump with a pass number in the file name and decl UIDs in the body; the golden signature extract in `scripts/check-optimization.sh` is the same evidence with a stable shape. Check 4's LTO band cannot exist: GCC's LTO objects are not byte-reproducible (two builds of one source differ), so LTO is asserted by correctness and execution instead. Identity is the stronger claim: identical machine code cannot be five percent slower for a reason attributable to the fact. |
| A-15 | 2026-10-19 | Phase 12 checks replaced by seven runnable commands (`scripts/check-c2crug.sh`, the report validator, `scripts/c2crug-diff.sh`, the `--analyze` report check, `make check-C2CRUG`, `scripts/check-diagnostics.sh`, `make check-C_COMPAT`); the corpus is `migration/` instead of `corpus/`; the "largely preserved" `VAGUE` marker is replaced by the enforced 20 percent churn and unchanged-comment-count band; ADR 0023 records the proof rules. | `corpus/` is thirty compile-only fragments (no Makefile, no test target, no output expectations, no allocation call), so the old prerequisite's "a C corpus project with its own green test suite at a pinned commit" could not be satisfied by it and checks 1-4 could not run at all: check 1 named a `make test` that no file provides, and `c2crug` itself did not exist. `migration/` supplies eight runnable programs with expected output, `make test` and a `.crugffi` sidecar, and every check is now one command with one exit status and a countable printed line. The churn band is 10 of 222 lines (4.5 percent) with comments 16 -> 16, so the owner's reading is now enforced rather than described. |
| A-16 | 2026-10-19 | Phase 13 checks replaced by seven runnable commands (`make -C libcrug check-PAL PAL=fake`, `make -C libcrug check-PAL PAL=linux`, `scripts/lint-no-os-apis.sh libcrug/core`, `scripts/check-pal.sh`, the `asm` scan wrapped in `test`, `make check-C_COMPAT`, `scripts/check-diagnostics.sh`); the prerequisite names ADR 0011 with `crug_pal.h`; the architecture path is `libcrug/arch/`; the contract is twelve cases and the fake backend's guard-page decline is a documented skip; ADR 0011 records the poller deferral. | The draft gate could not be adjudicated as written. Checks 1 and 2 named `make -C libcrug check-PAL`, but `libcrug` did not exist and the suite behind it was unspecified, so "runs contract tests" named no number and no case. Check 3's `grep -R asm libcrug/core \| grep -vc '^$'` exits 1 when the count is zero — a clean core reads as a failure — so it is wrapped in `test`. Check 4 put architecture code at `libcrug/pal/arch/`, while MASTER_SPEC section 26.1 and RFC-0001 section 36 separate the OS axis (`libcrug/pal/`) from the CPU axis (`libcrug/arch/`); nothing exists at either path in this phase, and the gate now asserts the layout the later phases will use. The roadmap's poller work item is deferred by this phase's own non-goal, so the poller is not checked and ADR 0011 D2 records why. Parity between the two backends, the guard-page fault and the absence of a system symbol in the fake object are new checks; the diagnostic count is 111. |
| A-17 | 2026-10-19 | Phase 14 checks replaced by eight runnable commands (`make check-GOROUTINES`, the 100000-task stress run, the `strace` clone count wrapped in `test`, the sanitizer suite, the kernel-compat scripts, `scripts/lint-no-os-apis.sh libcrug/core`, `scripts/check-diagnostics.sh`, `make check-C_COMPAT`); the prerequisite names ADR 0008/0009/0013 and the two phase documents. | The draft gate could not be adjudicated as written. Check 3 passed `-c` to `strace`, which prints one summary line, so `grep -c clone` returned 1 whatever the worker count; it now counts call lines and compares the count with `test` to 4, which fails both on 1 and on 100000. Check 5 named `gcc/testsuite/crug.dg/kernel/hello.o`, but the kernel corpus is `k01.c` to `k18.c` plus `freestanding.c` and no `hello.c` exists, so the command could only fail; the phase-2 kernel scripts assert the same property (`__crug_=0`, `libcrug=0`) on real objects. Checks 1 and 2 named observables in prose (`task-loss counter 0`, `stack-corruption guard 0`) that `scripts/gate.sh` cannot read; they are now the exact printed lines. The lint and the diagnostic registry are new checks because the phase adds five codes and a consumer of the PAL. |
| A-18 | 2026-10-19 | Phase 15 checks replaced by seven runnable commands (`make check-SCOPE`, two rejection compiles for `CONC-015`/`CONC-016`, the sanitizer suite, `scripts/lint-no-os-apis.sh libcrug/core`, `scripts/check-diagnostics.sh`, `make check-C_COMPAT`); the prerequisite names ADR 0024 and `SCOPE.md`; the observable for the group counters is `leaked task group state: 0` printed by the stress program. | The draft gate could not be adjudicated as written. Checks 1-4 were prose rather than commands, and three of them named artefacts that did not exist: check 1 asserted "an assertion on the completion order" with no command or expected line, check 2 named `gcc/testsuite/crug.dg/scope/child-outlives.c` (the rule is a lifetime rule and lives in `scope-borrow-lifetime.c` as `CONC-016`; a goto into a scope block is the separate `CONC-015`), check 3 named a path `gcc/testsuite/crug.dg/scope/owned-capture` that is not a command and printed nothing, and check 4 ran `./build/scope-stress` under `ASAN_OPTIONS`, a binary no target instrumented and no target built. The rewritten checks each have one command, one exit status and one countable printed line, and the sanitizer check is the one that actually instruments the group API (`scripts/check-tasks-sanitizer.sh` builds and runs `libcrug/tests/scope_stress.c` under ASan/UBSan/LeakSanitizer). |
| A-19 | 2026-10-19 | Phase 16 checks replaced by seven runnable commands (`make check-CHANNELS`, three `crugc -fsyntax-only` cases for the channel layout and the `CONC-003`/`CONC-017` rejections, `scripts/lint-no-os-apis.sh libcrug/core`, `scripts/check-diagnostics.sh`, `make check-C_COMPAT`); the prerequisite names ADR 0025 and `CHANNELS.md`; the ownership-transfer observable is the `channels-owned: text=hi releases=1` line. | The draft named a `make check-SELECT` target that does not exist, a path (`gcc/testsuite/crug.dg/channels/owned-send`) where a command was needed, and two binaries (`build/select-waiter-stress`, `build/mpmc-stress`) that the tree never builds; its "fairness policy" and "waiter cancellation" cases are also outside the implemented surface (a rotation counter, no cancellation). Each check is now one command with one printed observable that `scripts/gate.sh` can adjudicate. |
| A-20 | 2026-10-20 | Phase 17 checks replaced by seven runnable commands (`make check-SEND_SYNC`, four `crugc -fsyntax-only` cases for the raw-pointer, owned-transfer, non-Sync-borrow and non-Send-element rules, `scripts/check-diagnostics.sh` and `make check-C_COMPAT`); the prerequisite names ADR 0026 and `SEND-SYNC.md`; the thread-affinity pair is exercised inside `scripts/check-send-sync.sh` (affinity clause present => `CONC-025`, absent => exit 0); the printed observable is the `send-sync: 14 compiler test(s) ...` line. | The draft could not be adjudicated as written. Check 1 was prose ("with compile-fail cases for ..."), check 2 left the outcome open ("either rejected ... or accepted with the documented unmanaged warning") so no exit status could be compared, and check 3 named a path (`gcc/testsuite/crug.dg/send-sync/affine-handle`) where a command was needed and no such file exists - the affinity pair is two compilations against two sidecars, now inside the script. The policy is fixed by ADR 0026 D2: a raw pointer is rejected with `CONC-024`. |
| A-21 | 2026-10-20 | Phase 18 checks replaced by six runnable commands (`make check-MULTI_OS`, `scripts/check-multi-os-ports.sh`, the core `#if` scan wrapped in `test`, `make -C libcrug check-PAL PAL=fake`, `scripts/lint-no-os-apis.sh libcrug/core`, `make check-C_COMPAT`); the prerequisite names ADR 0012, `PORTABILITY.md` and `.github/workflows/multi-os.yml`; the AArch64 arm moves from `make check-MULTI_OS TARGET=aarch64-linux` into the port script's container route; the `VAGUE` marker is removed. | The draft could not be adjudicated as written. Check 1 claimed `exit 0 on linux-x86_64, windows-x86_64 and macos-arm64`, which `scripts/gate.sh` cannot attribute to three hosts from one command, and two of the three are not this host. Check 3's `grep -vc '^$'` exits 1 when the count is zero, so a clean core read as a failure — the same defect A-16 fixed. Check 4 needs an `aarch64-linux-gnu-gcc` this host does not have, so the cross target moved into `scripts/check-multi-os-ports.sh`, which uses `crug-arm64-build` when present and the host toolchain otherwise, runs the Windows x86-64 build under Wine too, and exits 2 rather than passing silently when no port can run. Check 3 named `PAL=<platform>`, leaving the backend open; `PAL=fake` is the one run that proves the contract is backend-agnostic, the Linux backend is already covered by check 1, and Darwin is covered by the `macos-14` CI job. The `VAGUE` marker on "ifdef sprawl" is replaced by the exact zero-match test. The 2+ worker crash that the two non-Linux ports exposed was fixed by pinning a task to the first worker that ran it (ADR 0009 amendment A-1), which the port script now keeps honest by asserting the fix is present in the staged core. |
| A-22 | 2026-10-20 | Phase 19 checks replaced by six runnable commands: the K1-K7 stage loop, the three produced-artefact tests, the `nm` scan wrapped in `test`, the K6 boot line, and the object-diff plus P0 pair; the prerequisite names `kernel/KERNEL-CORPUS.md`, `qemu-system-x86_64`, the K7 cross toolchain and the per-phase gate timeout; the `VAGUE` item is replaced by the pinned sizes (K3 106 objects, K4 438, K7 15). | The draft gate could not be adjudicated as written. Check 2 named `make CC=crugc` with no object directory and no wrapper, so the command did not exist; check 3's qemu line contained `...` placeholders and no initramfs, so it could not reach userspace; check 4 ran `nm vmlinux` from an unspecified working directory; `scripts/kernel-object-diff.sh` and `scripts/check-p0-open.sh` did not exist at all; and stage K4 was mentioned outside the loop while stages K1, K2, K3, K5, K6 and K7 shared one loop with no per-stage observable. Each item is now one command with one exit status and one printed line, the corpus sizes are pinned in `kernel/KERNEL-CORPUS.md`, and the 120 second gate default is called out because the K5 full build and the first K7 cross build legitimately exceed it. |
| A-23 | 2026-10-20 | Phase 20 checks replaced by six runnable commands (`scripts/check-diagnostics.sh`, the 50-row count wrapped in `test`, `make check-DIAGNOSTICS`, `scripts/check-diagnostic-sarif.sh`, the `GIMPLE` count wrapped in `test`, `make check-docs-examples`); the machine-readable check moves from `-fdiagnostics-format=json` plus `jq` to SARIF `ruleId` and origin-note results; the prerequisite names the corpus list, the launcher, `python3` and `docs/crug/MIGRATION-EXAMPLES.md`; the `VAGUE` item is replaced by the exact-50 definition. | The draft could not be adjudicated as written. Check 1's `wc -l` counted comment lines and printed no exit status of its own; check 2's `make check-DIAGNOSTICS` named a target that did not exist; check 3 used `-fdiagnostics-format=json`, a format GCC 16.2 does not implement (it has SARIF only), so the `jq` pipeline was unfalsifiable; check 4's bare `grep -c ... => 0` exits 1 on zero matches unless wrapped in `test` (the defect A-16 fixed); and none of the checks named the corpus path or proved a golden exists per class. The code now carries the stable code as a SARIF `ruleId` and the origin notes as results, which is what `scripts/check-diagnostic-sarif.sh` checks; `scripts/check-diagnostic-goldens.sh` proves the 50 goldens and the origin-note column, and `docs/crug/DIAGNOSTICS.md` documents SARIF as the only machine format. |
| A-24 | 2026-10-20 | Phase 21 checks replaced by six runnable commands (`bench/run.py --zero-cost --check`, `--pristine --check`, `--plain-c --check`, `--concurrency --check`, `scripts/check-kernel-perf.sh`, `make check-PERFORMANCE`), each with one printed line and one exit status; the prerequisite names the pristine compiler, the recorded baseline and the kernel corpus, and the time budget is stated; the `VAGUE` item is replaced by the exact sizes and by the pointer to `docs/crug/RELEASE-POLICY.md` section 2. | The draft could not be adjudicated as written. Check 1 said "show 0 additional instructions and 0 additional calls" with no command output to compare, and did not say which optimisation levels or which cases; check 2's "every ratio <= 1.02" contradicted the measured host swing and the identity rule that `bench/run.py` actually enforces (this host produced 0.935-1.131 on byte-identical objects); check 3 compared against "the C thread pool", a target the runtime cannot meet by design because a pool is allocation-free while Crug reserves a 64 KiB stack per task, so the comparison moved to pthread-per-task with the pool ratio recorded; check 4 named `make check-PERFORMANCE`, which did not exist. The pristine comparison against the unmodified GCC 16.2 is now a gate check because `diff-cc.sh` only compares two builds of the same fork and could not prove the fork changed nothing; the kernel codegen comparison is now a gate check rather than a note. Thresholds live in `RELEASE-POLICY.md` section 2, which the gate lines cite, so a number cannot be changed without an amendment row there and a re-run here. |
| A-25 | 2026-10-20 | Phase 22 checks replaced by four runnable commands (`make check-FUZZ`, `make check-SANITIZER`, the P0 plus regression-ledger pair, `scripts/check-fuzz-nightly.sh`), each with one printed line and one exit status; the prerequisite names the harnesses, the three checkers, the nightly workflow and the toolchain; the `VAGUE` window marker is replaced by `RELEASE-POLICY.md` section 3.3. | The draft could not be adjudicated as written. Check 1 named no command output and no budget, so no exit status could be compared; check 2's "TSan clean where fiber support is valid" left the exception undefined, and TSan cannot see a raw stack switch, so the TSan arm is confined to the OS-thread channel exchange and the exception is ADR 0028; check 3 required a "regression test id" but no ledger format and no checker existed; check 4 named `gh run list --json conclusion`, but `gh` is not installed on this host, the workflow name was a placeholder, push and scheduled runs were indistinguishable, no window length was defined, and the pipeline printed no exit status of its own. The checker defines the window precisely (consecutive successful scheduled runs; exit 0 met, 1 not met, 2 unavailable) and is what the gate runs. |


