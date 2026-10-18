# CRUG Project Status

Last updated: 2026-10-18

## Where the project stands

The repository contains specification text only. There is no compiler code, no
build system, no test suite, and no continuous integration. Four specification
documents exist (10,513 lines): the RFC, the master specification, the Rust memory
gaps analysis, and the production roadmap. An analysis of those documents produced
eight findings; this file tracks what has been resolved and what blocks progress.

## Resolved in this revision

| Item | Artefact |
|------|----------|
| Documents had no order of authority | spec/000-PRECEDENCE.md (authority order + conflict register C-01..C-10) |
| Context-sensitive words had no disambiguation rule | docs/crug/adr/0002-contextual-keywords.md |
| Aggregate ownership was undefined (double-drop hole) | docs/crug/adr/0004-aggregate-ownership.md |
| Destruction across goto/setjmp/exit/tasks was undefined | docs/crug/adr/0005-drop-control-flow.md |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No licence statement for a GCC fork | NOTICE |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-01 | Partial moves: forbidden in MVP vs required by GAPS s5 | owner ratification of ADR 0004 D4 |
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| C-06 | 27 phases vs 25 milestones, orphan milestones M10/M12, unlinked Phase 24 | ratification of EXIT-GATES.md |
| C-07 | Repository licence vs GPLv3 obligation of a GCC fork | owner decision |
| C-08 | "Strict mode" used but never defined | folded into ADR 0002/0004/0005 diagnostics; needs a line in LANGUAGE.md |
| C-09 | Kernel corpus for K0 does not exist | owner decision on kernel version + defconfig |
| -  | Syntax is sketched but ambiguous: MASTER_SPEC §8 gives intent-level EBNF and does not resolve qualifier attachment, `option<T>` angle brackets, `chan T name;` declarations, `go (expr);`, or ADT `enum` payloads | docs/crug/adr/0003-grammar.md (in preparation); RFC-0001 amendment after ratification |
| -  | No destructor syntax before this revision | resolved by ADR 0005 D2 |

## Next actions, in dependency order

1. **Ratify the four ADRs** (0002, 0004, 0005) and spec/000-PRECEDENCE.md. Nothing
   downstream is stable until the ownership model is decided.
2. **Decide C-07** (licence). This gates the first line of GCC-derived code.
3. **Write the grammar.** docs/crug/adr/0003-grammar.md turns the master spec's
   §8 sketches into implementation decisions: qualifier attachment, intrinsic
   forms, `go`/`scope`/`select`/`match` statement shapes, `chan T name;`,
   angle-bracket types in type context only, and the collision corpus. It is a
   prerequisite for TASK 008/010 and for any parser work.
4. **Amend RFC-0001** to cite the grammar and to adopt ADR 0004/0005 rules, so that
   conformance classes O1/B1 stop depending on implied semantics.
5. **Pin the environment.** docs/crug/ENVIRONMENT.md now records the GCC 16.2.0
   tarball checksum, host prerequisites and target triples; the kernel version
   and defconfig are still owner decisions (C-09) and must be added before any
   K0 claim.
6. Only then start Phase 0 per the roadmap.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done".
