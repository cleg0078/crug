# Phase 22 pre-closure gate run

This run is deliberately not a closure archive. It was taken at `d791347` on
2026-10-20, immediately after the `fuzz-nightly` workflow was pushed, to record
that the first three Phase 22 checks pass and to show the fourth check's honest
state:

| Check | Result |
| --- | --- |
| 001 `make check-FUZZ` | PASS |
| 002 `make check-SANITIZER` | PASS |
| 003 `./scripts/check-p0-open.sh && ./scripts/check-regressions.sh` | PASS |
| 004 `./scripts/check-fuzz-nightly.sh` | FAIL (rc=2, "the workflow has no scheduled runs yet") |

The nightly window needs 30 consecutive successful *scheduled* runs
(RELEASE-POLICY section 3.3; the first schedule is 03:17 UTC daily). When the
window exists, the gate is re-run and its logs are archived under the closing
commit's SHA. A pre-closure directory is never cited as evidence that M22 was
awarded.
