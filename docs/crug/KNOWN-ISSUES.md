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
<!-- ledger:end -->
```

## 3. Closed entries

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
