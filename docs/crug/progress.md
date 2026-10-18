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
13-25) and it adopts the ADR 0003/0004/0005 ownership and drop rules. Six decision records
(0001, 0002, 0003, 0004, 0005, 0018) and the precedence register were ratified on
2026-10-18, and the repository is now GPL-3.0-or-later. The master specification, roadmap
and gaps analysis still carry their original wording.

Reusable infrastructure now exists: `scripts/gate.sh` adjudicates every phase gate
and archives evidence, and the documentation set below gives the specification a
single authority order, a grammar, and decision records for the open questions.
The pinned GCC 16.2.0 tree is imported into git and tagged `releases/gcc-16.2`,
so the repository root is the GCC top level and the fork base is reproducible
from a clone rather than from a tarball on one machine.

## Resolved in this revision

| Item | Artefact |
|------|----------|
| Documents had no order of authority | spec/000-PRECEDENCE.md (authority order + conflict register C-01..C-11) |
| Context-sensitive words had no disambiguation rule | docs/crug/adr/0002-contextual-keywords.md |
| Aggregate ownership was undefined (double-drop hole) | docs/crug/adr/0004-ownership-analysis-layer.md |
| Ownership storage could have required hidden state | docs/crug/adr/0003-ownership-metadata-storage.md (static metadata, ABI erasure) |
| Destruction across goto/setjmp/exit/tasks was undefined | docs/crug/adr/0005-drop-lowering.md |
| Syntax was sketched but ambiguous | docs/crug/adr/0018-grammar.md |
| RFC-0001 stated syntax without deciding ambiguity, attachment or ABI erasure | spec/CRUG_RFC_0001.md 0.2-draft sections 13-25 + Appendix H |
| ADR numbering conflicted with MASTER_SPEC §79 | resolved 2026-10-18: ADR 0003/0004 split + ADR 0005 renamed; mapping in docs/crug/adr/README.md |
| The decision set was unratified, so nothing could be cited as settled | six ADRs and spec/000-PRECEDENCE.md carry Status Accepted (2026-10-18) |
| No kernel source was pinned, so K0 and Phase 19 were untestable | docs/crug/KERNEL-BASELINE.md (Linux 6.18.57 LTS, tag/commit/SHA256, x86_64_defconfig, qemu markers) + scripts/kernel-pin.sh; C-09 resolved 2026-10-18 |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (62 planned codes) + scripts/check-diagnostics.sh |
| No mechanism existed to evaluate a gate | scripts/gate.sh (`--list`, `--run`, archives per-check logs) |
| No upstream GCC was pinned | docs/crug/GCC-BASELINE.md + docs/crug/adr/0001-gcc-16.2-baseline.md + docs/crug/gcc-baseline.sha512 |
| The fork base lived outside git, so the tree could not be reproduced from a clone | GCC 16.2.0 imported as commit 57ccbae and tagged `releases/gcc-16.2`; `git diff releases/gcc-16.2..HEAD -- gcc/` is the fork delta |
| No way to reproduce the toolchain | Makefile (`make baseline`: verify, prerequisites, configure, all-gcc; `make build-full` adds target libraries and links; `make help`) |
| Repository licence was MIT, incompatible with the GCC fork | LICENSE (GPL-3.0-or-later), NOTICE (per-path map); C-07 resolved by the owner on 2026-10-18 |
| Line citations rotted as documents were revised | every citation now names a section or a gate phase and check (rule in spec/000-PRECEDENCE.md section 2) |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| C-06 | 27 phases vs 25 milestones, orphan milestones M10/M12, unlinked Phase 24 | ratification of EXIT-GATES.md |
| C-08 | "Strict mode" used but never defined | defined operationally by ADR 0018 D1/D10; needs a consolidated paragraph in docs/crug/STRICT.md |
| -  | The Phase-19 per-object corpus list `kernel/KERNEL-CORPUS.md` does not exist; the kernel source is pinned but the compile corpus is not | Phase 19 gate |
| -  | Phase-20 golden corpus (50 error classes with stable wording) is not written | Crug frontend; docs/crug/DIAGNOSTICS.md seeds the classes |
| -  | No ABI document; ADR 0003 D3 states the C-ABI rules but required ADR 0006 (C ABI policy) is unwritten | ADR 0006 |
| -  | The pre-fork baseline testsuite (phase-0 check 3) has never been run | dejagnu (`runtest`) is not installed and cannot be installed without root here |
| -  | `.crugffi` sidecar is illustrative only; required ADR 0007 is unwritten | ADR 0006 |

## Next actions, in dependency order

1. **Make the C testsuite runnable** (`make check-c` is currently an empty recipe)
   through a root-free local DejaGnu, with the run summarised by a script.
2. **Run Phase 0 for real** (`scripts/gate.sh --run phase-0`) and archive the log;
   the baseline testsuite run and the comparison against a pre-fork result set are
   the checks that still fail.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
