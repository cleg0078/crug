# Architecture Decision Records

Each ADR records one decision that the specification leaves open or contradicts.
Status values: Proposed (not yet ratified), Accepted, Superseded, Rejected.
A semantic change requires a new or amended ADR; editing a spec document alone is
not a decision. Ratified ADRs move to Accepted and stay citable by number.

## Written ADRs

Eighteen of the twenty written ADRs are Accepted and are cited by number
throughout `spec/` and `docs/crug/`. CLEG0078 (project owner) ratified six on
2026-10-18, ADRs 0006 and 0007 on 2026-10-19, ADRs 0019 and 0020 on 2026-10-19
as the Phase 7 planning decisions, ADR 0010 on 2026-10-19 together with the
5% phase-11 acceptance band, ADR 0023 on 2026-10-19 with the phase-12 corpus
and the "largely preserved" reading, and ADR 0011 on 2026-10-19 with the
phase-13 PAL v1 surface, ADRs 0008, 0009 and 0013 on 2026-10-19 with the
phase-14 task runtime, ADR 0024 on 2026-10-19 with the phase-15 structured
scope, and ADR 0025 on 2026-10-19 with the phase-16 channels and select
surface. ADR 0021 (Phase 8) is Proposed: it is implemented
and tested but awaits owner ratification. ADR 0022 (GCC baseline upgrade
policy) is Proposed: its measurements are recorded, but the policy itself awaits
ratification.

| ADR | File | Decides | Status |
|-----|------|---------|--------|
| 0001 | 0001-gcc-16.2-baseline.md | pinned GCC release, configure line, targets, evidence | Accepted (2026-10-18) |
| 0002 | 0002-contextual-keywords.md | recognition of Crug words and collision resolution | Accepted (2026-10-18) |
| 0003 | 0003-ownership-metadata-storage.md | static metadata, no hidden state, ABI erasure, forbidden representations | Accepted (2026-10-18) |
| 0004 | 0004-ownership-analysis-layer.md | owning aggregates, field places, drop order, unions, arrays, adoption | Accepted (2026-10-18) |
| 0005 | 0005-drop-lowering.md | where drops run, setjmp/exit/abort, task scopes | Accepted (2026-10-18) |
| 0010 | 0010-noalias-lowering.md | opt-in `restrict` fact from `mut`/`own`, caller obligations, 5% band, false-noalias stop conditions | Accepted (2026-10-19) |
| 0008 | 0008-goroutine-stack-model.md | one fixed private stack per task from the PAL, a low guard page, the canary, no growth, the copied frame | Accepted (2026-10-19) |
| 0009 | 0009-scheduler-model.md | M:N, one ready queue, cooperative scheduling, the epoch wait predicate, fatal spawn, idempotent shutdown | Accepted (2026-10-19) |
| 0011 | 0011-pal-contract.md | one OS-boundary header, v1 surface, error model, wait predicate, contract suite, backend parity, versioning | Accepted (2026-10-19) |
| 0013 | 0013-arch-context-switch-policy.md | the x86-64 SysV switch, the saved register set, stack alignment, no unwinding, compile-time arch selection, TSan limit | Accepted (2026-10-19) |
| 0018 | 0018-grammar.md | productions, qualifier attachment, go/scope/select/match, payload enums | Accepted (2026-10-18) |
| 0006 | 0006-c-abi-policy.md | two ABI classes, erasure, boundary moves, Crug-native registry, targets, versioning | Accepted (2026-10-19) |
| 0007 | 0007-ffi-metadata-format.md | `.crugffi` v1 grammar, clause semantics, discovery, versioning, trust model | Accepted (2026-10-19) |
| 0019 | 0019-interprocedural-borrow-policy.md | summary depth zero, borrows cross calls only as `ref`/`mut` arguments, no returned or stored borrows | Accepted (2026-10-19) |
| 0020 | 0020-last-use-liveness.md | a borrow ends at its last use; joins union, loops are conservative, `goto` abandons liveness | Accepted (2026-10-19) |
| 0021 | 0021-strict-mode-policy.md | strict mode is a checking mode, `raw`, `unsafe`, the five `crug-SAFE-*` restrictions and the two boundary call rules | Proposed (2026-10-19) |
| 0022 | 0022-gcc-baseline-upgrade-policy.md | baseline upgrade classes P/M/J, the acceptance matrix, the drift gate, version claims, who ratifies | Proposed (2026-10-19) |
| 0023 | 0023-c2crug-migration.md | c2crug proof rules, three modes, confidence, report schema, migration corpus, no invented safety | Accepted (2026-10-19) |
| 0024 | 0024-structured-scope.md | the scope statement as a task group, the wait policy, scoped borrowed captures, the goto rules, the additive runtime surface | Accepted (2026-10-19) |
| 0025 | 0025-channels-and-select.md | the chan/select representation, the select lowering, the ownership transfer rules, the additive runtime surface | Accepted (2026-10-19) |

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
| 0006 c-abi-policy | written and accepted as `0006-c-abi-policy.md` |
| 0007 ffi-metadata-format | written and accepted as `0007-ffi-metadata-format.md` |
| 0010 noalias-lowering | written and accepted as `0010-noalias-lowering.md` |
| 0011 pal-contract | written and accepted as `0011-pal-contract.md` |
| 0008 goroutine-stack-model | written and accepted as `0008-goroutine-stack-model.md` |
| 0009 scheduler-model | written and accepted as `0009-scheduler-model.md` |
| 0013 arch-context-switch-policy | written and accepted as `0013-arch-context-switch-policy.md` |
| 0012, 0014-0017 | not written |

## Candidate next ADRs

| Number | Topic | Depends on |
|--------|-------|------------|
| 0012 | OS backend policy | ADR 0011 |
| 0014 | Portable I/O poller | a phase that needs I/O |
| 0026 | the next topic above 0017 (channels took 0025) | a phase that needs it |
| 0016 | Freestanding and no-runtime policy | ADR 0017 |
| 0017 | Kernel ownership policy | C-09 (kernel pin) |

ADR 0007 defines the `.crugffi` v1 format that Phase 9's parser implements, and
both it and ADR 0006 are accepted, so Phase 9 is unblocked on the document side.
ADRs 0019 and 0020 record the two Phase 7 policies (interprocedural depth zero and
last-use liveness). ADR 0022 keeps the GCC pin revisable on evidence; a major
baseline upgrade would supersede ADR 0001 with the next free number (0025 today).
ADR 0010 is written and accepted, so Phase 11 is unblocked on the document side;
ADR 0023 is written and accepted, so Phase 12 is unblocked on the document side.
ADR 0011 is written and accepted, so Phase 13 is unblocked on the document side
and its gate is adjudicable. ADRs 0008, 0009 and 0013 are written and accepted,
so Phase 14 is unblocked on the document side and its gate is adjudicable (the
scheduler model they record carries Phase 16 as well). ADR 0024 is written and
accepted, so Phase 15 is unblocked on the document side and its gate is
adjudicable. ADR 0025 is written and accepted, so Phase 16 (channels and
select) is unblocked on the document side and its gate is adjudicable; the next
free number above 0017 is 0026. The next ADRs that unblock work are 0012 and
0014 (OS backend and poller, when a second port or an I/O phase opens) and
0015-0017 (kernel compatibility and ownership); 0016 and 0017 follow the kernel
ownership work.

## Writing rules

- One decision per ADR; a decision that depends on another names it in the header.
- State what the ADR forbids, not only what it permits; that is what a reviewer tests.
- Every decision gets a test path under `gcc/testsuite/crug.dg/`.
- A new ADR starts Proposed; status changes to Accepted only by the owner, recorded
  with a date, and the change is committed with the ADR itself.
