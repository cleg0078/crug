# Architecture Decision Records

Each ADR records one decision that the specification leaves open or contradicts.
Status values: Proposed (not yet ratified), Accepted, Superseded, Rejected.
A semantic change requires a new or amended ADR; editing a spec document alone is
not a decision. Ratified ADRs move to Accepted and stay citable by number.

## Written ADRs

All six were ratified on 2026-10-18 by CLEG0078 (project owner) and are cited by
number throughout `spec/` and `docs/crug/`.

| ADR | File | Decides | Status |
|-----|------|---------|--------|
| 0001 | 0001-gcc-16.2-baseline.md | pinned GCC release, configure line, targets, evidence | Accepted (2026-10-18) |
| 0002 | 0002-contextual-keywords.md | recognition of Crug words and collision resolution | Accepted (2026-10-18) |
| 0003 | 0003-ownership-metadata-storage.md | static metadata, no hidden state, ABI erasure, forbidden representations | Accepted (2026-10-18) |
| 0004 | 0004-ownership-analysis-layer.md | owning aggregates, field places, drop order, unions, arrays, adoption | Accepted (2026-10-18) |
| 0005 | 0005-drop-lowering.md | where drops run, setjmp/exit/abort, task scopes | Accepted (2026-10-18) |
| 0018 | 0018-grammar.md | productions, qualifier attachment, go/scope/select/match, payload enums | Accepted (2026-10-18) |

ADR 0003 is the metadata half of the former aggregate-ownership record, ADR 0004
the analysis half; the split resolved conflict C-11 (recorded in
`spec/000-PRECEDENCE.md`). ADR 0004 keeps decision numbers D1 and D4 to D10 so
that citations made before the split still resolve; the former D2 and D3 now live
in ADR 0003 as D2 and D3.

## Numbering

MASTER_SPEC section 79 requires seventeen ADRs with fixed file names:
0001-gcc-16.2-baseline, 0002-contextual-keywords, 0003-ownership-metadata-storage,
0004-ownership-analysis-layer, 0005-drop-lowering, 0006-c-abi-policy,
0007-ffi-metadata-format, 0008-goroutine-stack-model, 0009-scheduler-model,
0010-noalias-lowering, 0011-pal-contract, 0012-os-backend-policy,
0013-arch-context-switch-policy, 0014-portable-io-poller,
0015-linux-kernel-c-compatibility, 0016-freestanding-no-runtime-policy,
0017-kernel-ownership-policy.

Rules: a required topic keeps its required number; a topic that is not on the list
takes the next free number above 0017; no number is reused. Grammar is not on the
list, so it is 0018. ADR 0003, 0004 and 0005 now carry the required file names.

## State of the required list

| Required | State |
|----------|-------|
| 0001 | written and accepted |
| 0002 | written and accepted |
| 0003 ownership metadata storage | written and accepted as `0003-ownership-metadata-storage.md` |
| 0004 ownership analysis layer | written and accepted as `0004-ownership-analysis-layer.md` |
| 0005 drop lowering | written and accepted as `0005-drop-lowering.md` |
| 0006-0017 | not written |

## Candidate next ADRs

| Number | Topic | Depends on |
|--------|-------|------------|
| 0006 | C ABI policy: layout, calling convention, borrow and slice representation | ADR 0003, ADR 0004, `docs/crug/ABI.md` |
| 0007 | `.crugffi` metadata format and threat model | ADR 0003 D3, ADR 0006 |
| 0008 | Goroutine stack model | ADR 0018 D4/D5 |
| 0010 | Aliasing and the noalias permission model | ADR 0004 |
| 0011 | PAL contract | ADR 0006 |
| 0016 | Freestanding and no-runtime policy | ADR 0017 |
| 0017 | Kernel ownership policy | C-09 (kernel pin) |

ADR 0006 is the next one that unblocks work; 0009, 0012 to 0015 have no written
dependency yet and should be scheduled with the phase that needs them.

## Writing rules

- One decision per ADR; a decision that depends on another names it in the header.
- State what the ADR forbids, not only what it permits; that is what a reviewer tests.
- Every decision gets a test path under `gcc/testsuite/crug.dg/`.
- A new ADR starts Proposed; status changes to Accepted only by the owner, recorded
  with a date, and the change is committed with the ADR itself.
