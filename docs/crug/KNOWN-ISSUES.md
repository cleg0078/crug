# CRUG known-issue ledger

**Document type:** Machine-readable defect ledger
**Status:** Living
**Owner:** CRUG project owner (compiler lead). Gate adjudicator: `scripts/check-p0-open.sh`.
**Derived from:** `spec/CRUG_PRODUCTION_ROADMAP.md` sections 3 and 35.3 (severity
definitions and stop conditions), `spec/CRUG_MASTER_SPEC_EN.md` section 56 and
`docs/crug/EXIT-GATES.md` (phases 19, 24 and 25 read this ledger).

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
<!-- ledger:end -->
```

## 3. Closed entries

- **K-001** (P0, runtime, fixed in Phase 18): the scheduler migrated a task
  between worker threads; a C frame resumed elsewhere still held the address of
  the old thread's `_Thread_local` storage, so `crug_task_finish` read a NULL or
  foreign `current_task` and wrote through it. Fixed by pinning a task to the
  worker that first runs it (ADR 0009 amendment A-1); the regression is
  `scripts/check-multi-os-ports.sh` (2/4/8 workers on Windows and AArch64) plus
  `libcrug/tests/tasks_main.c`.
