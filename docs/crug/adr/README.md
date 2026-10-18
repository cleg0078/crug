# Architecture Decision Records

Each ADR records one decision that the specification leaves open or contradicts.
Status values: Proposed (not yet ratified), Accepted, Superseded, Rejected.
A semantic change requires a new or amended ADR; editing a spec document alone is
not a decision. Ratified ADRs move to Accepted and stay citable by number.

## Written ADRs

| ADR | File | Decides | Status |
|-----|------|---------|--------|
| 0001 | 0001-gcc-16.2-baseline.md | pinned GCC release, configure line, targets, evidence | Proposed |
| 0002 | 0002-contextual-keywords.md | recognition of Crug words and collision resolution | Proposed |
| 0004 | 0004-aggregate-ownership.md | move/copy of aggregates, memcpy, arrays, unions, ABI boundaries | Proposed |
| 0005 | 0005-drop-control-flow.md | where drops run, setjmp/exit/abort, task scopes | Proposed |
| 0018 | 0018-grammar.md | productions, qualifier attachment, go/scope/select/match, payload enums | Proposed |

## Numbering (conflict C-11)

MASTER_SPEC section 79 (L4266-4283) requires seventeen ADRs with fixed file names:
0001-gcc-16.2-baseline, 0002-contextual-keywords, 0003-ownership-metadata-storage,
0004-ownership-analysis-layer, 0005-drop-lowering, 0006-c-abi-policy,
0007-ffi-metadata-format, 0008-goroutine-stack-model, 0009-scheduler-model,
0010-noalias-lowering, 0011-pal-contract, 0012-os-backend-policy,
0013-arch-context-switch-policy, 0014-portable-io-poller,
0015-linux-kernel-c-compatibility, 0016-freestanding-no-runtime-policy,
0017-kernel-ownership-policy.

Rules: a required topic keeps its required number; a topic that is not on the list
takes the next free number above 0017; no number is reused. Grammar is not on the
list, so it is 0018. The earlier proposal in this file numbered grammar as 0003;
that proposal is withdrawn.

## State of the required list

| Required | State |
|----------|-------|
| 0001 | written |
| 0002 | written |
| 0003 ownership metadata storage | partially in ADR 0004 D1-D3; own file not written |
| 0004 ownership analysis layer | ADR 0004 covers both 0003 and 0004 topics; splitting is DECISION-REQUIRED (C-11) |
| 0005 drop lowering | written as 0005-drop-control-flow.md; content matches, file name differs |
| 0006-0017 | not written |

## Candidate next ADRs

| Number | Topic | Depends on |
|--------|-------|------------|
| 0003 | Ownership metadata storage: how ownership state is represented without hidden fields (split of ADR 0004) | C-11 decision |
| 0004 | Ownership analysis layer: dataflow, joins, diagnostic set (split of ADR 0004) | C-11 decision |
| 0006 | C ABI policy: layout, calling convention, borrow and slice representation | ADR 0004, `docs/crug/ABI.md` |
| 0007 | `.crugffi` metadata format and threat model | ADR 0004 D3, ADR 0006 |
| 0008 | Goroutine stack model | ADR 0018 D4/D5 |
| 0010 | Aliasing and the noalias permission model | ADR 0004 |
| 0011 | PAL contract | ADR 0006 |
| 0016 | Freestanding and no-runtime policy | ADR 0017 |
| 0017 | Kernel ownership policy | C-09 (kernel pin) |

## Writing rules

- One decision per ADR; a decision that depends on another names it in the header.
- State what the ADR forbids, not only what it permits; that is what a reviewer tests.
- Every decision gets a test path under `gcc/testsuite/crug.dg/`.
- Status changes to Accepted only by the owner, recorded with a date.
