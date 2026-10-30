# CRUG known-issue ledger

**Document type:** Machine-readable defect ledger
**Status:** Living
**Owner:** CRUG project owner (compiler lead). Gate adjudicator: `scripts/check-p0-open.sh`.
**Derived from:** `spec/CRUG_PRODUCTION_ROADMAP.md` sections 3 and 35.3 (severity
definitions and stop conditions), `spec/CRUG_MASTER_SPEC_EN.md` section 56 and
`docs/crug/EXIT-GATES.md` (phases 19, 22, 24 and 25 read this ledger).

The gate documents ask for "0 open P0" without defining where a P0 is recorded.
This file is that record. It is deliberately small: an entry is added when a
defect is confirmed at P0 or P1, and its status changes when it is fixed. A
severity is not a label for "we have not looked"; the roadmap section 35.3 stop
conditions list what counts as P0 (miscompilation, ABI break, kernel runtime
leakage, ownership unsoundness, double drop, false noalias, scheduler memory
corruption).

## 1. Format

The ledger is the region between the two marker lines. Each entry is one line
with six fields, separated by spaces; the summary is the rest of the line:

```
<id> <severity> <scope> <status> <regression> <summary>
```

| Field | Values |
|-------|--------|
| id | `K-<n>`, never reused |
| severity | `P0`, `P1`, `P2`, `P3` (roadmap section 3) |
| scope | `kernel`, `runtime`, `compiler`, `ffi`, `docs`, `ci` |
| status | `open`, `fixed`, `wontfix` |
| regression | path to the test that pins the fix, or `-` while open |
| summary | one sentence |

`scripts/check-p0-open.sh` counts open `P0` entries (all scopes, or `--kernel`
for the kernel build qualification) and exits non-zero when one exists. It also
reports fixed P0/P1 entries that have no regression test, which phase 24's gate
consumes.

## 2. Ledger

```
<!-- ledger:begin -->
# id    severity scope   status regression                          summary
K-001   P0       runtime fixed  scripts/check-multi-os-ports.sh        a task kept a stale cached _Thread_local address when it resumed on another worker (Phase 18, ADR 0009 A-1)
K-002   P1       compiler fixed gcc/testsuite/crug.dg/ownership/error-mark-body.c  a parse error left error_mark_node as the function body and crug_check_function called BIND_EXPR_BODY on it (ICE, found by the Phase 22 fuzz harness)
K-003   P1       runtime  fixed libcrug/tests/runtime/chan_ticket.c  a capacity-0 send completed on the shared slot being empty, so a second sender's deposit kept the first sender blocked after its value had been received (R4-RT-001)
K-004   P1       compiler fixed scripts/check-c2crug-proofs.sh  c2crug --safe treated a conditional free as proof and could insert destruction around a branch; only a straight-line unconditional free proves safety now (R4-TOOL-001)
K-005   P2       compiler fixed migration/proofs/guard-valid.c  c2crug credited a null guard with 0.80 confidence without proving that the exit it controls dominates the release (R4-TOOL-002)
K-006   P1       runtime  fixed libcrug/tests/runtime/shutdown_status.c  shutdown from inside a task mutated runtime state instead of refusing; try_shutdown now returns 2 before any mutation and the void wrapper keeps its contract (R4-RT-002)
K-007   P2       runtime  fixed libcrug/tests/runtime/recv_scratch.c  a channel receive in a task could read through another task's shared thread-local buffer after a migration (AUD-001)
K-008   P2       runtime  fixed libcrug/tests/runtime/recv_scratch.c  the receive buffer was thread-local, so two tasks on one worker aliased the same storage; receive now uses per-task scratch (AUD-007)
K-009   P2       compiler fixed gcc/testsuite/crug.dg/channels/select-claim.c  select lowered to a polling loop that could consume a value it then failed to deliver; each arm now claims at most once (AUD-002)
K-010   P2       compiler fixed gcc/testsuite/crug.dg/channels/select-claim-owned.c  select on an owning channel could claim a value and lose the ownership transfer; the owning arm is claimed or not taken, never both (AUD-004)
K-011   P1       ci       fixed scripts/gate.sh  a gate CHECK that was never executed could still be counted as success; the driver now fails on a skipped check and has a self-test (AUD-003)
K-012   P1       ci       fixed scripts/check-gate-archives.sh  an incomplete archive was accepted as green; the checker now requires every recorded check and a self-test pins the rule (AUD-009)
K-013   P2       runtime  fixed libcrug/tests/runtime/shutdown_concurrent.c  two concurrent shutdowns could both become the owner; a second caller now waits for stopping and reports status 3 on join failure (AUD-005)
K-014   P1       runtime  fixed libcrug/tests/runtime/spawn_shutdown.c  spawn admission checked stopping outside the scheduler lock, so a task could be queued into a shutdown and dropped; admission is now atomic and fatal during shutdown (P3-007)
K-015   P2       runtime  fixed libcrug/tests/runtime/frame_alignment.c  a captured frame size was rounded without preserving the alignment of the captured structure, and zero-capture frames were not covered (AUD-006)
K-016   P2       compiler fixed gcc/testsuite/crug.dg/borrow/borrow-into-global.c  borrow destinations stored a raw pointer without tracking the referent, so a borrow into a global or static escaped the lattice (AUD-008)
K-017   P1       compiler fixed gcc/testsuite/crug.dg/borrow/join-switch.c  the borrow lattice was not joined at a CFG join, so a lender could be used after the join that released it (P3-001)
K-018   P2       compiler fixed gcc/testsuite/crug.dg/alias/conditional.c  alias provenance was not joined at a conditional, so an unproven root could be reported disjoint (P3-002)
K-019   P2       compiler fixed gcc/testsuite/crug.dg/alias/copy-chain.c  alias identity was taken from the declaration rather than the proven base, so a copy chain hid two views of one object (P3-003)
K-020   P2       compiler fixed gcc/testsuite/crug.dg/alias/indirect-call.c  an indirect call bypassed the exclusivity proof for its pointer arguments; such calls now keep the aliasing facts unproven (P3-004)
K-021   P2       ffi      fixed gcc/testsuite/crug.dg/ffi/phase9/bad/dtor-arity.c  a .crugffi destructor was resolved without validating its arity or variadic tail; it now requires an external prototype with exactly one pointer parameter matching the pointee (P3-005)
K-022   P2       ffi      fixed gcc/testsuite/crug.dg/ffi/phase9/bad/argref-malformed.c  a malformed argN clause was accepted; it now accepts only arg or ret followed by decimal digits, no sign or trailing text, with the index inside int range (P3-006)
K-023   P1       compiler fixed gcc/testsuite/crug.dg/grammar/qualifier-lookahead-5.c  four chained contextual qualifiers overflowed the parser's four-token buffer and crashed; the buffer is 16 tokens with a checked bound (P3-008)
K-024   P1       compiler fixed  gcc/testsuite/crug.dg/ownership/cond-expr-drop.c  the ownership pass walked the operands of ?:, && and || in sequence, so an arm that skips a drop lost the destructor and a program that drops in both arms was rejected; each arm is now analysed as an alternative and joined, and a conditional move or initializer picks its destructor per arm (F-01)
K-025   P2       compiler fixed  gcc/testsuite/crug.dg/ownership/switch-move-arms.c  a switch whose default covers every path still joined the pre-switch state into each arm, so a valid switch that drops or moves in every arm was rejected; the state now flows from each case label entry and the pre-switch state is dropped when every default arm breaks (CR-02)
K-026   P1       ffi      fixed  gcc/testsuite/crug.dg/ffi/metadata/argref-space.c  parse_argref kept a pointer into a destroyed std::string for the argN clause, so the reported line and the accepted metadata could change with heap reuse; the string now outlives the loop (CR-01)
K-027   P2       ffi      fixed  scripts/check-ffi-suite.sh  a metadata file with an embedded NUL byte was tokenised as if it ended there, so a diagnostic could name a different line; the file is scanned before tokenising and crug-FFI-012 reports it (F-02)
K-028   P2       ffi      fixed  gcc/testsuite/crug.dg/ffi/metadata/returns-unowned.c  the grammar prose made `owned` mandatory on a `returns` clause while the parser also accepts a plain type; ADR 0007 A-3 now states the optional form and a fixture pins it (F-03)
K-029   P2       ffi      fixed  scripts/check-ffi-suite.sh  a metadata file could declare an unbounded number of entries; a per-file cap of 10000 now reports crug-FFI-010 (F-04)
K-030   P1       compiler fixed  gcc/testsuite/crug.dg/channels/recv-over-aligned.c  a non-VLA receive loaded the element from the shared 16-byte-aligned scratch, so a type aligned to 32/64/128 was read through a misaligned access (UBSan); recv now lowers to crug_chan_recv_into/_owned into a temporary of the element type, with libcrug/tests/channels/recv-align.c as the committed run arm (C-01)
K-031   P2       runtime  open   -  the legacy per-thread scratch of crug_chan_recv_ptr/_owned is retained after a detached external thread exits (LSan: 4 MiB in 16 blocks); accepted and documented, a PAL TLS destructor needs contract 1.3 (C-02, follow-up task-14)
K-032   P2       runtime  fixed  libcrug/tests/runtime/recv_scratch_big.c  the per-task receive scratch cache kept an element larger than the cache limit for the life of the task (4.2 MB observed); a large scratch is freed at task exit and crug_task_recv_scratch_aligned reuses only a buffer that fits the requested alignment (A-01)
K-033   P1       runtime  fixed  libcrug/tests/runtime/scope_wake_race.c  a delayed PAL wake could target a freed scope whose address was reused, delivering a wake to the new scope; scope leave now completes the group before releasing it and no wake is issued after that handshake (R-01)
K-034   P2       runtime  fixed  libcrug/tests/runtime/stats_race.c  crug_task_stats_get read queue_peak and configured_workers as plain memory while the scheduler wrote them under its lock (TSan); both fields are atomic now (R-02)
K-035   P1       runtime  fixed  libcrug/tests/runtime/startup_shutdown_race.c  two shutdown attempts during the first worker creation could both fail with status 3 and then trip crug-CONC-029; start and stop now publish their state under the scheduler lock and the creator is woken once (S-01)
K-036   P2       ci       fixed  scripts/run-coverage.sh  a coverage archive was accepted when an input omitted the commit field, so a measurement could be labelled with a HEAD it never measured; the field is now mandatory (exit 2 when absent, 1 when it differs) with a self-test (CR-03)
K-037   P1       compiler fixed  migration/witnesses/hidden-alias.c  c2crug --safe reasoned on the raw text, so a release hidden behind a macro and reached through a hidden alias could be treated as proven and the migration introduced a use-after-free; eligibility now requires a token-identical preprocessed body with no added or removed exit (S-02/C2-A)
K-038   P2       compiler fixed  scripts/test-c2crug.py  a sidecar whose first line did not declare `crugffi <major>.<minor>;` was still used as evidence; a sidecar with an unknown major is ignored with a message (C2-B)
K-039   P2       compiler fixed  migration/bench-c2crug.py  --analyze rescanned the source for every candidate, so 800 candidates cost about 5.6 s and 1200 about 12.5 s; per-function fact caching makes the same inputs take 0.067/0.110/0.255/0.728 s for 100/200/400/800 pairs (C2-C)
K-040   P2       compiler fixed  migration/witnesses/same-line-exits.c  two returns on one line made the guard of the first cover the second, so a release could be migrated although an exit skipped it; the proof now matches the terminal statement position (R4-TOOL-003)
K-041   P2       compiler fixed  gcc/testsuite/crug.dg/channels/recv-owned-drop.c  'own T *v = recv (ch); drop (v);' was rejected as crug-OWN-007 because the ownership pass only recognised a CALL_EXPR initialiser; a received owned value is now modelled as adopted with no destructor and only the unreleased and double-drop forms are diagnosed (C-01 follow-up)
K-042   P2       ci       open   -                                      the diagnostic scripts resolve crugc from PATH before the in-tree launcher, so 'make check-FFI' validated an installed 0.2.3 compiler instead of this tree; scripts/gate.sh prepends gcc/ so archived gates are unaffected
K-043   P0       compiler fixed  gcc/testsuite/crug.dg/ownership/do-while-double-move.c  a do-while body was walked once, so a move was invisible to the back edge and iteration two double-freed (OWN-01)
K-044   P1       compiler fixed  gcc/testsuite/crug.dg/ownership/do-while-leak.c  the missing back-edge join also leaked an adopt overwrite and made release-then-read a use-after-release (OWN-02/OWN-02b; use-after-drop covered by the sibling fixture)
K-045   P1       compiler fixed  gcc/testsuite/crug.dg/alias/global-escape.c  an alias fact injected restrict although the lender had been copied to a global before the call, a deterministic -O2 miscompile (OWN-03)
<!-- ledger:end -->
```

## 3. Closed entries

- **fix-p2 batch** (K-024 to K-041, fixed 2026-10-28 on top of `c0dcb18`):
  eighteen findings from the `spec/fix-p2` audit: seventeen are fixed with one
  regression each and one (K-031) is an accepted, documented limitation. The
  compiler group is the conditional ownership join (K-024, the `drop` on one arm
  of a conditional or `&&`/`||` expression), the `switch` move join
  (K-025) and the owned receive (K-041: `own T *v = recv (ch); drop (v);` no
  longer reports `crug-OWN-007`); the FFI group is the hoisted argument
  separator (K-026), the NUL byte and the truncated metadata (K-027, K-029), the
  optional `owned` on `returns` (K-028, ADR 0007 A-3) and the entry cap; the
  channel group is the receive into caller storage, which removes the shared
  scratch misalignment (K-030) and trims that scratch to 64 KiB (K-032), with
  the external-thread TLS retention accepted and documented (K-031, open until
  PAL 1.3 can carry a destructor); the runtime group is the scope wake race
  (K-033), the `queue_peak` data race (K-034) and the start/shutdown handshake
  (K-035); and the process group is the coverage provenance field (K-036) and
  `c2crug --safe` (K-037 to K-040: preprocessed input, the sidecar major, the
  per-function fact cache and the same-line exit proof). The Crug runtime ABI
  stays 1; `crug_chan_recv_into`/`_owned` are additive and the PAL contract is
  unchanged at 1.2.

- **fix-p3 batch** (K-043 to K-045, fixed 2026-10-30 on top of `c43060459`):
  the fix-p3 audit carries 25 identifiers (22 confirmed findings and three
  observations it marks SUSPECTED or OBSERVÉ). The ledger records the three P0/P1
  entries; the full adjudication is in `spec/fix-p3/LEDGER.md`. The compiler group is
  the do-while ownership back edge (K-043, K-044), the escaped-lender alias fact
  (K-045), file-scope `own` and `own` arrays (`crug-OWN-014`, `crug-TYPE-015`), the
  FFI sidecar that ends inside a declaration (`crug-FFI-013`), depth-32 Send/Sync
  inference and affinity through an aggregate member; the runtime group is the scope
  completion handshake, the runtime start order and the PAL port edges; and the
  process group is the c2crug scanner/atomic `--safe` publication, the version and
  maintainer provenance, the SPDX symbolic links and `clean-dist`, plus the parser
  fuzzing seeds. `TYP-01` (a negative `slice` length is stored wrapped) is accepted
  by design. The Crug and runtime ABI, `.crugffi` and the PAL contract are unchanged;
  the batch is unreleased under `[Unreleased]`.

- **Post-rc1 batch** (K-003 to K-023, fixed 2026-10-26 on top of f86b7df):
  twenty-one defects from the external audit (four R4 and seventeen prior
  findings) were fixed with one regression each. The runtime group is the
  capacity-0 ticket handoff (K-003), the checked shutdown and its
  use-from-a-task refusal (K-006, K-013), atomic spawn admission (K-014), the
  per-task receive scratch (K-007, K-008) and the captured-frame alignment
  (K-015); the compiler group is the borrow join (K-017), alias provenance
  (K-018 to K-020), the stricter FFI-011 resolution (K-021, K-022) and the
  lookahead bound (K-023); the tooling group is the conditional-free proof
  (K-004, K-005); and the process group is the two gate false-success
  mechanisms (K-011, K-012), each with a negative self-test. The Crug runtime
  ABI stays 1; only the PAL ABI moves to 1.2 (`join` now returns `int`).

- **K-002** (P1, compiler, fixed in Phase 22): a function definition that fails
  to parse (for example `int f (void) * x { return 0; }`) can leave
  `error_mark_node` as the body, and the ownership pass called
  `BIND_EXPR_BODY` on it: `tree check: expected bind_expr, have error_mark in
  crug_check_function`. The fuzz harness found it as 46 crashes across the
  parser, ownership and borrow targets. Fixed by returning early from
  `crug_check_function` when the body is `error_mark_node`; the regression is
  `gcc/testsuite/crug.dg/ownership/error-mark-body.c`, and the fuzz harness must
  stay at 0 crashes for the fix to count.

- **K-001** (P0, runtime, fixed in Phase 18): the scheduler migrated a task
  between worker threads; a C frame resumed elsewhere still held the address of
  the old thread's `_Thread_local` storage, so `crug_task_finish` read a NULL or
  foreign `current_task` and wrote through it. Fixed by pinning a task to the
  worker that first runs it (ADR 0009 amendment A-1); the regression is
  `scripts/check-multi-os-ports.sh` (2/4/8 workers on Windows and AArch64) plus
  `libcrug/tests/tasks_main.c`.

## 4. P1 waivers

`docs/crug/RELEASE-POLICY.md` section 4 allows at most two open P1 findings at
the release-candidate tag. Each one needs a waiver row here, signed by the
project owner: the row names the entry, who signed it, the date the decision was
taken, the affected scope and the reason the finding is acceptable for the
candidate. `scripts/count-open-p1.sh --max=2` reads this table; an open P1 with
no row, a row with no signature, and a row that does not name an open P1 all
fail the check. There is no waiver mechanism for 1.0 (`--max=0`).

```
<!-- waivers:begin -->
| id | signed by | date | scope | reason |
|----|-----------|------|-------|--------|
<!-- waivers:end -->
```
