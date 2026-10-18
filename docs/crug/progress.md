# CRUG Project Status

Last updated: 2026-10-18

## Where the project stands

The repository contains specification text, a phase-gate adjudicator and a
reproducible GCC build; there is no compiler code, no CRUG test suite, and no
continuous integration. Four specification
documents exist (10,513 lines): the RFC, the master specification, the Rust memory
gaps analysis, and the production roadmap. An analysis of those documents produced
eight findings; this file tracks what has been resolved and what blocks progress.

The RFC has since been revised to 0.2-draft: its surface grammar is now normative (sections
13-25) and it adopts the ADR 0004/0005 ownership and drop rules. The master specification,
roadmap and gaps analysis still carry their original wording.

Reusable infrastructure now exists: `scripts/gate.sh` adjudicates every phase gate
and archives evidence, and the documentation set below gives the specification a
single authority order, a grammar, and decision records for the open questions.

## Resolved in this revision

| Item | Artefact |
|------|----------|
| Documents had no order of authority | spec/000-PRECEDENCE.md (authority order + conflict register C-01..C-11) |
| Context-sensitive words had no disambiguation rule | docs/crug/adr/0002-contextual-keywords.md |
| Aggregate ownership was undefined (double-drop hole) | docs/crug/adr/0004-aggregate-ownership.md |
| Destruction across goto/setjmp/exit/tasks was undefined | docs/crug/adr/0005-drop-control-flow.md |
| Syntax was sketched but ambiguous | docs/crug/adr/0018-grammar.md |
| RFC-0001 stated syntax without deciding ambiguity, attachment or ABI erasure | spec/CRUG_RFC_0001.md 0.2-draft sections 13-25 + Appendix H |
| ADR numbering conflicted with MASTER_SPEC §79 | spec/000-PRECEDENCE.md C-11 + docs/crug/adr/README.md |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (62 planned codes) + scripts/check-diagnostics.sh |
| No mechanism existed to evaluate a gate | scripts/gate.sh (`--list`, `--run`, archives per-check logs) |
| No upstream GCC was pinned | docs/crug/GCC-BASELINE.md + docs/crug/adr/0001-gcc-16.2-baseline.md + docs/crug/gcc-baseline.sha512 |
| No way to reproduce the toolchain | Makefile (`make baseline`: download, verify, extract, prerequisites, configure, all-gcc; `make build-full` adds target libraries and links; `make help`) |
| Repository licence was MIT, incompatible with the GCC fork | LICENSE (GPL-3.0-or-later), NOTICE (per-path map); C-07 resolved by the owner on 2026-10-18 |
| Line citations rotted as documents were revised | every citation now names a section or a gate phase and check (rule in spec/000-PRECEDENCE.md section 2) |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-01 | Partial moves: forbidden in MVP vs required by GAPS s5 | owner ratification of ADR 0004 D4 |
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| C-06 | 27 phases vs 25 milestones, orphan milestones M10/M12, unlinked Phase 24 | ratification of EXIT-GATES.md |
| C-08 | "Strict mode" used but never defined | defined operationally by ADR 0018 D1/D10; needs a consolidated paragraph in docs/crug/STRICT.md |
| C-09 | Kernel corpus for K0 does not exist | owner decision on kernel version + defconfig |
| C-11 | ADR 0004 covers two required ADRs (0003 metadata storage, 0004 analysis layer) | owner decision on splitting |
| -  | Phase-20 golden corpus (50 error classes with stable wording) is not written | Crug frontend; docs/crug/DIAGNOSTICS.md seeds the classes |
| -  | No ABI document; ADR 0004 D3 states the C-ABI rules but required ADR 0006 (C ABI policy) is unwritten | ADR 0006 |
| -  | The pre-fork baseline testsuite (phase-0 check 3) has never been run | dejagnu (`runtest`) is not installed and cannot be installed without root here |
| -  | `.crugffi` sidecar is illustrative only; required ADR 0007 is unwritten | ADR 0006 |

## Next actions, in dependency order

1. **Ratify the five ADRs** (0001, 0002, 0004, 0005, 0018) and spec/000-PRECEDENCE.md.
   Nothing downstream is stable until the ownership model and the grammar are decided.
2. **Decide C-11** (split ADR 0004 into required 0003 + 0004 files) while the text is
   still fresh; splitting after ratification costs a re-review.
3. **Pin the kernel** (C-09): version + commit, defconfig, qemu boot command, console
   marker, object list. Without it K0 is unclaimable.
4. **Run Phase 0 for real** (`scripts/gate.sh --run phase-0`) and archive the log.
   `make baseline` supplies the pinned build; the phase-0 checks that still fail are
   the baseline testsuite run and the comparison against a pre-fork result set,
   because neither has been produced yet. The gate's two baseline checks were
   aligned with the Makefile targets (`make verify`, built `xgcc`).

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
