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
**Prerequisite:** Phase 5 gate archived at the same commit — roadmap §10 blocking gate: no borrowing phase is production-grade until drop correctness is stable.
**Exit gate:**
- `CHECK: make check-BORROW` => exit 0, with compile-fail cases for mut+ref, mut+mut, move-while-borrowed, drop-while-borrowed, and pass cases for multiple shared borrows and one mutable borrow.
- `CHECK: objdump -d gcc/testsuite/crug.dg/borrow/borrow-cost.o | grep -cE 'atomic|__sync|lock'` => 0 extra operations versus the same function written with plain pointers (no refcount).
- `CHECK: nm gcc/testsuite/crug.dg/borrow/borrow-cost.o | grep -cE 'pthread_mutex|__crug_lock'` => 0 (no hidden lock behind `mut`).
- `CHECK: crugc -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/borrow/mut-plus-ref.c` => exit 1 with a stable diagnostic code.
**Non-goals:** no NLL/last-use (P7), no interprocedural lifetimes, no Send/Sync (P17), no runtime borrow tracking, no changes to plain-C pointer rules.
**Risk of skipping:** a borrow checker built before drop is exact will accept programs whose cleanup paths the IR cannot prove, producing UAF that reviewers trust because "borrow checked".

## Phase 7 — CFG Borrowing and Non-Lexical Lifetimes
**Closes:** M7 — NLL / realistic borrowing.
**Prerequisite:** Phase 6 gate archived; liveness/last-use analysis present in the borrow pass (visible in `-fdump-crug=borrow`).
**Exit gate:**
- `CHECK: make check-BORROW` => exit 0 including the NLL corpus: last use ends the borrow, mutable reborrow after a dead shared borrow compiles, invalid escaping borrow is rejected at its use site.
- `CHECK: crugc -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/borrow/real-c/*/*.c 2>&1 | grep -c 'internal compiler error'` => 0 on the realistic-C corpus.
- `CHECK: crugc -fcrug-borrow -fsyntax-only gcc/testsuite/crug.dg/borrow/escape-return.c` => exit 1 with both the use site and the borrow origin quoted in the notes.
- `CHECK: make check-C_COMPAT && make check-KERNEL_COMPAT` => exit 0 (no regression when borrow is off).
**Non-goals:** no lifetimes on struct fields beyond the documented rule, no full dataflow across translation units, no unsafe-escape valve (P8), no optimizer use of borrow facts (P11).
**Risk of skipping:** a lexical-only checker rejects valid kernel-style code, and users ship with `-fcrug-borrow` off — the safety claim becomes decorative.
`VAGUE:` "limited interprocedural summaries" sets no limit; weakest defensible check: any borrow crossing a function boundary must fail closed (reject, never silently accept) and that rejection is a golden test. The owner must state the summary depth in an ADR.

## Phase 8 — Strict Mode, `raw`, and `unsafe`
**Closes:** M8 — Strict/unsafe model.
**Prerequisite:** Phase 7 gate archived; a policy ADR covering raw dereference, integer-pointer conversion, owned→unmanaged conversion, inline asm, `setjmp`/`longjmp` and unknown FFI.
**Exit gate:**
- `CHECK: crugc -fcrug-strict -fsyntax-only gcc/testsuite/crug.dg/strict/raw-deref.c` => exit 1 with a stable strict-mode code; the same file without `-fcrug-strict` => exit 0.
- `CHECK: scripts/diff-obj.sh gcc/testsuite/crug.dg/strict/ "-O2" "-O2 -fcrug-strict"` => 0 differences on programs with no unmanaged operations (`unsafe` has zero runtime cost).
- `CHECK: make check-STRICT_UNSAFE` => exit 0, including a `longjmp` case asserting the documented policy (reject cleanup past `longjmp`, or the ADR's alternative) and an inline-asm ownership case.
- `CHECK: make check-KERNEL_COMPAT` => exit 0 (plain kernel C unchanged when strict is off).
**Non-goals:** no memory safety inside `unsafe`, no provenance or UB sanitizer, no `restrict`-style optimizer facts (P11), no removal of hybrid C mode.
**Risk of skipping:** without an explicit escape hatch users disable the whole checker at the first false positive, leaving no boundary to audit.

## Phase 9 — Production C FFI Layer
**Closes:** M9 — C FFI production layer.
**Prerequisite:** Phase 5 and Phase 8 gates archived; `.crugffi` grammar v1 ADR accepted and the metadata parser test corpus exists. ADR 0007 (Accepted 2026-10-19) defines that format, and the parser corpus it lists is the test corpus.
**Exit gate:**
- `CHECK: make check-FFI` => exit 0 for ownership-return, consuming-arg, shared/mutable borrow, out-owned, nullability, destructor, callback, and conflicting-metadata diagnostics.
- `CHECK: sha256sum lib/libsqlite3.so.0 lib/libcurl.so.4 lib/libssl.so.3 lib/libc.so.6` => digests identical before and after building the four integration tests (no recompilation of the C library).
- `CHECK: gcc/testsuite/crug.dg/ffi/integration-sqlite` => runs, and the owned handle from `sqlite3_open` is freed exactly once (`allocs=1 frees=1`).
- `CHECK: crugc -fcrug-ffi=gcc/testsuite/crug.dg/ffi/conflict.crugffi -fsyntax-only gcc/testsuite/crug.dg/ffi/conflict.c` => exit 1 with a deterministic, stable error code (repeat twice, byte-identical stderr).
- `CHECK: scripts/diff-obj.sh gcc/testsuite/crug.dg/ffi/ "-O2" "-O2 -fcrug-ffi=gcc/testsuite/crug.dg/ffi/libc.crugffi"` => 0 differences (metadata never changes binary ABI).
**Non-goals:** no ABI changes to annotated libraries, no run-time metadata loading, no general binding generator, no blocking/thread-affinity enforcement (placeholder fields only; enforced at P17).
**Risk of skipping:** FFI metadata that perturbs codegen breaks the "existing .so/.a need no recompilation" promise and every integration claim with it.
`VAGUE:` "several real external libraries" is unspecified; weakest defensible check: exactly the four named in §14 (libc, SQLite, libcurl-or-SDL, OpenSSL-or-FFmpeg). The owner must not substitute easier libraries after the fact.

## Phase 10 — Safe Core Types
**Closes:** M10 — Safe core types.
**Prerequisite:** Phase 9 gate archived (slice/FFI lowering rules) and Phase 5 gate archived (active-field drop for ADTs).
**Exit gate:**
- `CHECK: make check-SAFE_TYPES` => exit 0 for 10A slices, 10B Option, 10C Result, 10D ADTs, including bounds-check on/off modes and non-exhaustive `match` rejection.
- `CHECK: crugc -fsyntax-only gcc/testsuite/crug.dg/safe-types/layout.c` => exit 0, where the file `_Static_assert`s every size/alignment recorded in `docs/crug/ABI.md`.
- `CHECK: gcc/testsuite/crug.dg/safe-types/adts-drop` => prints the active-variant destructor calls only (`drop=1` per value, inactive branches `drop=0`).
- `CHECK: crugc -fsyntax-only gcc/testsuite/crug.dg/safe-types/non-exhaustive-match.c` => exit 1 with an exhaustiveness diagnostic naming the missing variant.
- `CHECK: make check-C_COMPAT` => exit 0 (plain C pointers unaffected).
**Non-goals:** no generics or traits (§32, post-1.0), no niche optimization (explicitly "later"), no `?` operator, no ABI stability promise for Crug-native types beyond `ABI.md`.
**Risk of skipping:** types shipped before their layout is written down become de-facto ABI that cannot be fixed without a 2.0.

## Phase 11 — Ownership-Aware Optimization
**Closes:** M11 — Optimizer-aware ownership.
**Prerequisite:** Phase 7 gate archived (proven `mut` exclusivity, NLL soundness) and the three-arm benchmark matrix (C ordinary / C restrict / Crug ref-mut) exists under `bench/`.
**Exit gate:**
- `CHECK: bench/run.py --matrix alias --check` => every Crug ref/mut case within 5% of its hand-written `restrict` twin (ratio ≤ 1.05) on at least one benchmark.
- `CHECK: crugc -O2 -fdump-tree-alias gcc/testsuite/crug.dg/optimization/noalias-golden.c` => restrict/noalias annotations appear only where the `mut` proof exists; golden diff 0.
- `CHECK: make check-OPTIMIZATION` => exit 0, including adversarial-alias and LTO correctness suites.
- `CHECK: bench/run.py --plain-c --check` => plain-C ratios ≤ 1.02 versus the recorded baseline GCC at -O0/-O2/-O3/LTO (not systematically degraded).
- `CHECK: ./scripts/check-stop-conditions.sh` => 0 false-noalias reports (a false noalias fact is a P0 and stops the phase).
**Non-goals:** no new GIMPLE opcodes, no alias facts from unproven ownership, no vectorizer changes outside justified facts, no "Crug must beat C" target (§26).
**Risk of skipping:** a false noalias is a silent miscompilation with no user-visible symptom until the program is wrong in production.
`VAGUE:` "restrict-like optimization" has no number; the weakest defensible check is the 5% band above, ratified by the owner in an ADR before P11 closes.

## Phase 12 — `c2crug` Migration Tool Integration
**Closes:** M12 — c2crug migration tooling.
**Prerequisite:** Phase 9 gate archived (metadata v1 stable) and Phase 5 gate archived; a C corpus project with its own green test suite exists at a pinned commit.
**Exit gate:**
- `CHECK: c2crug --safe corpus/ -o out/ && crugc -fcrug-ownership -fcrug-borrow -c out/*.c` => exit 0, and `make -C out test` => exit 0 with the corpus's original assertions intact.
- `CHECK: c2crug --safe corpus/ --report report.json` => JSON validates against the published schema and exits 0; every inserted `own` has a report entry with a confidence value and an evidence location.
- `CHECK: scripts/c2crug-diff.sh corpus/ out/` => transformed lines ≤ 20% of total and comment-line count unchanged (comments and structure largely preserved).
- `CHECK: c2crug --analyze corpus/ | grep -c 'raw'` => ambiguous ownership appears as `raw` plus a report entry, never as silent `own` (no invented safety).
**Non-goals:** no whole-program automatic migration, no borrow inference beyond suggestions, no crugc behaviour changes, no claim that migrated code is memory-safe (`no invented safety guarantees`, §17).
**Risk of skipping:** a tool that upgrades unproven C to "safe Crug" destroys the trust premise (§41) faster than any compiler bug.
`VAGUE:` "largely preserved" is undefined; the 20% churn and unchanged-comment-count checks are this file's weakest defensible reading and must be ratified by the owner.

## Phase 13 — Platform Abstraction Layer (PAL)
**Closes:** M13 — PAL stable.
**Prerequisite:** Phase 9 and Phase 5 gates archived (so `libcrug` builds against a real FFI/drop model); PAL v1 contract ADR (`crug_pal.h`) accepted.
**Exit gate:**
- `CHECK: make -C libcrug check-PAL PAL=fake` => exit 0 (scheduler core compiles and runs contract tests against the fake PAL).
- `CHECK: make -C libcrug check-PAL PAL=linux` => exit 0.
- `CHECK: scripts/lint-no-os-apis.sh libcrug/core` => 0 matches for `pthread_|epoll|futex|CreateThread|kqueue|WaitOnAddress`.
- `CHECK: grep -R 'asm' libcrug/core | grep -vc '^$'` => 0 (architecture-dependent context code lives only under `libcrug/pal/arch/`).
**Non-goals:** no scheduler, tasks or channels (P14–P16), no second OS port (P18), no pollers/io_uring (post-1.0, §32), no PAL ABI promise beyond v1.
**Risk of skipping:** OS calls leak into the core, every port becomes a scheduler fork, and `class T1` (RFC §7.6) becomes unprovable.

## Phase 14 — Goroutine / Task Runtime MVP
**Closes:** M14 — Goroutine runtime MVP.
**Prerequisite:** Phase 13 gate archived (PAL v1 frozen); x86-64 SysV context-switch design reviewed against the ABI and recorded in an ADR.
**Exit gate:**
- `CHECK: make check-GOROUTINES` => exit 0 including spawn, yield, completion and shutdown suites.
- `CHECK: GOROUTINES_STRESS=1 ./build/tasks-stress` => 100000 trivial tasks complete, task-loss counter 0, stack-corruption guard 0.
- `CHECK: strace -f -e trace=clone,clone3 -c ./build/tasks-stress 2>&1 | grep -c clone` => OS worker count equals the configured N (default 4–8), not 100000.
- `CHECK: ASAN_OPTIONS=abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 make -C libcrug check-sanitizer` => exit 0.
- `CHECK: nm -u gcc/testsuite/crug.dg/kernel/hello.o | grep -c '__crug_'` => 0 (plain C and kernel builds still link no runtime).
**Non-goals:** no work stealing, no growable stacks, no preemption, no timers/fds, no structured scope (P15), no channels (P16), no Windows/macOS context (P18).
**Risk of skipping:** a "temporary" one-OS-thread-per-task design becomes the architecture, falsifying the M:N claim in §2.6 and `class T1`.

## Phase 15 — Structured Concurrency
**Closes:** M15 — Structured concurrency.
**Prerequisite:** Phase 14 gate archived (stress suite green); Phase 7 gate archived (borrow facts available for scoped captures).
**Exit gate:**
- `CHECK: make check-SCOPE` => exit 0, including scope-waits-for-all-children with an assertion on the completion order.
- `CHECK: crugc -fcrug-goroutines -fsyntax-only gcc/testsuite/crug.dg/scope/child-outlives.c` => exit 1 (a child cannot outlive its scope), with a stable code.
- `CHECK: gcc/testsuite/crug.dg/scope/owned-capture` => each captured owned value prints `frees=1` (transfer, not copy or double drop).
- `CHECK: ASAN_OPTIONS=detect_leaks=1 ./build/scope-stress` => exit 0 with `leaked task group state: 0`.
**Non-goals:** no full cancellation trees or error aggregation beyond the baseline policy, no detached tasks, no task-local storage, no channels/select (P16).
**Risk of skipping:** unstructured tasks make every borrowed capture unsound and later turn channel shutdown into undefined lifetime territory.

## Phase 16 — Channels and `select`
**Closes:** M16 — Channels/select.
**Prerequisite:** Phase 15 gate archived; Phase 5 gate archived (owned-send exactly-once depends on deterministic drop).
**Exit gate:**
- `CHECK: make check-CHANNELS && make check-SELECT` => exit 0 (buffered, unbuffered, close, send, recv, multiple cases, default, fairness policy, waiter cancellation).
- `CHECK: gcc/testsuite/crug.dg/channels/owned-send` => `allocs=1 frees=1` across producer and consumer (ownership transferred exactly once).
- `CHECK: ASAN_OPTIONS=detect_leaks=1 ./build/select-waiter-stress` => exit 0, 0 leaked waiters, 0 use-after-free in the waiter teardown.
- `CHECK: MPMC_STRESS_ITERS=100000 ./build/mpmc-stress` => exit 0 with no deadlock and no lost elements.
**Non-goals:** no fairness guarantee beyond the documented policy, no cancellation of blocked selects beyond the baseline, no channel `range`/iteration sugar, no lock-free performance claims (P21).
**Risk of skipping:** channel ownership duplicated or a waiter UAF surfaces as random memory corruption in user code, and the runtime loses credibility at Beta.

## Phase 17 — Send/Sync-Like Static Concurrency Safety
**Closes:** M17 — Send/Sync concurrency safety.
**Prerequisite:** Phase 16 gate archived (channels exist to carry values) and Phase 9 gate archived (thread-affinity metadata fields).
**Exit gate:**
- `CHECK: make check-SEND_SYNC` => exit 0, with compile-fail cases for: non-sendable value crossing a worker boundary, non-sync shared borrow, thread-affine handle where migration is possible.
- `CHECK: crugc -fcrug-goroutines -fsyntax-only gcc/testsuite/crug.dg/send-sync/raw-ptr.c` => outcome matches the documented rule exactly (raw pointers gain no false safety; either rejected as today, or accepted with the documented unmanaged warning).
- `CHECK: gcc/testsuite/crug.dg/send-sync/affine-handle` => compiles only when the `.crugffi` affinity field is absent, exits 1 with a stable code when present.
**Non-goals:** no Rust-equivalent auto-trait inference beyond the documented inference, no dynamic checks, no runtime lock injection, no kernel-mode concurrency checking.
**Risk of skipping:** without static Send/Sync the channel and `go` features ship a data-race generator that the ownership model appears to bless.

## Phase 18 — Multi-OS Runtime
**Closes:** M18 — Multi-OS runtime.
**Prerequisite:** Phase 14–16 gates archived on Linux; PAL contract tests green; CI runners for windows-x86_64 and macos-arm64 exist and are required.
**Exit gate:**
- `CHECK: make check-MULTI_OS` => exit 0 on linux-x86_64, windows-x86_64 and macos-arm64; task and channel suites included.
- `CHECK: grep -RE '#if.*(_WIN32|__APPLE__|__linux__)' libcrug/core | grep -vc '^$'` => 0 (no platform ifdefs in the scheduler core).
- `CHECK: make -C libcrug check-PAL PAL=<platform>` => exit 0 for each platform, from one PAL contract suite.
- `CHECK: make check-MULTI_OS TARGET=aarch64-linux` => exit 0 (Linux AArch64 PAL + AArch64 context validated).
**Non-goals:** no BSD before 1.0 (policy decision, §23), no IOCP/io_uring/kqueue advanced backends (master-spec P19, post-1.0), no RISC-V, no cross-OS binary compatibility.
**Risk of skipping:** the §2.7 portability promise is unverified, and the first Windows user discovers that the "portable" core is a Linux scheduler.
`VAGUE:` "no platform-specific ifdef sprawl" has no threshold; 0 matches in `libcrug/core` is the weakest defensible reading, ratified by the owner.

## Phase 19 — Linux Kernel Build Qualification
**Closes:** M19 — Linux kernel build milestone.
**Prerequisite:** Phase 2 gate archived (stage K0 microtests green in mandatory CI) plus the pinned kernel source of `docs/crug/KERNEL-BASELINE.md` and a Kbuild wrapper (roadmap §24 stage K5: `make CC=crugc ...`).
**Exit gate:**
- `CHECK: for s in K1 K2 K3 K5 K6 K7; do ./scripts/kernel-stage.sh $s; done` => each stage exits 0; stage K4 uses the pinned corpus list in `kernel/KERNEL-CORPUS.md`.
- `CHECK: make CC=crugc -j$(nproc)` (pinned kernel tree) => `vmlinux` produced, 0 errors.
- `CHECK: qemu-system-x86_64 -kernel arch/x86/boot/bzImage ... | grep -c 'crug-boot-ok'` => 1 (stage K6 boot smoke reaches the marker).
- `CHECK: nm vmlinux | grep -c '__crug_'` => 0 (no mandatory runtime symbols).
- `CHECK: ./scripts/kernel-object-diff.sh` => every object-level difference versus GCC is either absent or has a recorded explanation; `./scripts/check-p0-open.sh --kernel` => 0.
**Non-goals:** no kernel ownership contracts (master-spec 8K), no RCU-aware modeling, no kernel task backend, no second-architecture boot (stage K7 compiles/validates a subset only).
**Risk of skipping:** full build and boot are the only evidence that the syntax-compatibility baseline generalizes; without them the kernel claim is a microtest story.
`VAGUE:` stages K3 ("one subsystem") and K4 ("substantial cross-section") name no size; the owner must publish the pinned file list and counts in `kernel/KERNEL-CORPUS.md` before M19 is claimed.

## Phase 20 — Diagnostics, Tooling, Developer Experience
**Closes:** M20 — Tooling/diagnostics hardening.
**Prerequisite:** gates M11, M18 and M19 archived (§33 dependency graph); a stable diagnostic-code registry exists.
**Exit gate:**
- `CHECK: scripts/check-diagnostics.sh` => exit 0 (the registry is consistent before goldens are added).
- `CHECK: wc -l < gcc/testsuite/crug.dg/diagnostics/top50.txt` => 50 named error classes, each with a golden file.
- `CHECK: make check-DIAGNOSTICS` => golden diff 0 (wording stable) and every case carries an origin note.
- `CHECK: crugc -fdiagnostics-format=json ... | jq -e '.[] | select(.code and .notes)'` => exit 0 (machine-readable diagnostics with codes and notes for all 50).
- `CHECK: grep -c 'GIMPLE' docs/crug/DIAGNOSTICS.md` => 0 in the fix instructions (no common error requires reading GIMPLE dumps).
- `CHECK: make check-docs-examples` => exit 0 (migration examples in the docs compile and run).
**Non-goals:** no full LSP server, no formatter implementation (decision only), no debugger front-end, no syntax-highlighting grammar completeness beyond the decision record.
**Risk of skipping:** safety features nobody can act on are turned off project-wide, which negates P4–P11 in practice.
`VAGUE:` "top 50 common errors" is not defined by the roadmap; this file's weakest defensible reading is a published 50-entry list with goldens, ratified by the owner.

## Phase 21 — Performance Qualification
**Closes:** M21 — Performance qualification.
**Prerequisite:** Phase 18 and Phase 19 gates archived; `bench/` harness plus a recorded GCC 16.2 baseline on named reference hardware.
**Exit gate:**
- `CHECK: bench/run.py --zero-cost --check` => own/move/ref/mut cases show 0 additional instructions and 0 additional calls versus the plain-C twin (IR and assembly diff).
- `CHECK: bench/run.py --plain-c --check` => every -O0/-O2/-O3/LTO ratio ≤ 1.02 against the recorded baseline.
- `CHECK: bench/run.py --concurrency --check` => task dispatch within 1.5× of the C thread pool and memory-per-task below the pthread-per-task arm; channel latency/throughput/memory/scaling recorded.
- `CHECK: make check-PERFORMANCE` => exit 0 with results published under `bench/results/<date>.md`.
**Non-goals:** no "Crug must beat C" target (§26), no micro-optimization of the compiler itself, no new optimizations introduced here (only measurement and sign-off), no kernel codegen rework.
**Risk of skipping:** unmeasured zero-cost claims are marketing, and §2.8 ("no hidden runtime overhead") stays unprovable at release.
`VAGUE:` "competitive with design goals" carries no numbers; the 1.5×/1.02/5% thresholds above are placeholders and must be ratified in `docs/crug/RELEASE-POLICY.md` before M21 is claimed.

## Phase 22 — Security and Reliability Qualification
**Closes:** M22 — Security/reliability qualification.
**Prerequisite:** Phase 21 gate archived; fuzz harnesses and corpora (parser, ownership, borrow, `.crugffi`, scheduler, channel state machine) exist and run in CI.
**Exit gate:**
- `CHECK: make check-FUZZ` => all six harnesses run their configured budget with 0 crashes, 0 timeouts, 0 new corpus findings.
- `CHECK: make check-SANITIZER` => ASan and UBSan clean; TSan clean where fiber support is valid, with any exception recorded in an ADR.
- `CHECK: ./scripts/check-p0-open.sh && ./scripts/check-regressions.sh` => 0 open P0 and 0 historical P0/P1 bugs without a regression test id.
- `CHECK: gh run list --workflow=fuzz-nightly --limit=30 --json conclusion` => 30 consecutive `success`, 0 unmitigated findings.
**Non-goals:** no formal verification, no proof of memory safety inside `unsafe`, no third-party audit unless ADR'd, no new features during hardening.
**Risk of skipping:** an RC with a latent P0 miscompilation is the worst failure mode in §3 and cannot be patched out of a 1.0 tag.
`VAGUE:` the qualification window is "e.g. 30 days … a release-management decision"; this file uses 30 consecutive green days as the weakest defensible window and requires the owner to confirm it.

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

The same pattern may exist in other VAGUE markers (12 in this file). A VAGUE marker
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


