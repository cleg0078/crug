# CRUG Project Status

Last updated: 2026-10-19

## Where the project stands

The repository contains specification text, a phase-gate adjudicator, a
reproducible GCC build and, from Phase 1, the `crugc` driver; there is no Crug
syntax implementation, no `libcrug`, and no Crug test suite. The `kernel-compat`
CI job builds the pinned baseline on every push and is a required check on `main`.
Four specification
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
from a clone rather than from a tarball on one machine. The C testsuite runs
through a root-free DejaGnu (`scripts/bootstrap-dejagnu.sh`), and its 224,481-pass
result is archived at `docs/crug/baseline/phase0/`; the phase-0 and phase-1 gates
pass with archived checks, and `docs/crug/EXIT-GATES.md` was ratified on 2026-10-19,
so the gate definitions are binding and the phase-to-milestone mapping (C-06) is
settled. `gcc/crugc` is a launcher around the built GCC driver
for now, so plain C compatibility is measured rather than assumed
(`make check-C_COMPAT`, `scripts/diff-cc.sh`). Phase 2 is closed:
`-fcrug-kernel` is accepted and ignored, the 21 roadmap work items run against a
named kernel-style corpus (`make check-KERNEL_COMPAT`), object scans show no
`__crug_` symbol and no `libcrug` dependency, and freestanding units carry no
hidden runtime (`scripts/check-kernel-freestanding.sh`). The CI half runs:
`.github/workflows/kernel-compat.yml` builds the pinned baseline on every push,
and branch protection on `main` requires the `kernel-compat` check, so Phase 2
and M2 closed on 2026-10-19 at commit `78c759b`. The repository is public since
2026-10-19, which is what makes free branch protection and the faster public
runner available.

Phase 3 is implemented: the ten `-fcrug-*` options are declared and closed over
their implications, the feature macros exist only in Crug mode, and the flag
machinery changes no parse and no object byte. `docs/crug/FLAGS.md` is the
digest; `scripts/check-crug-flags.sh` (149 work items) and
`scripts/check-gnu-c-compat.sh` (50 files) are the local checks. The
productions the flags will gate - `own`, `move`, `borrow`, `slice`, `chan`,
`match` - do not exist yet: that is Phase 4 onward, and the contextual words
still parse as ordinary C identifiers under every combination. The Phase 3 gate
passes its four checks at commit `78c759b` (evidence under
`docs/crug/gates/phase-3/78c759b/`), and its prerequisite - the `kernel-compat`
job being mandatory at HEAD - is met, so M3 closed on 2026-10-19.

The ABI question now has a policy: ADR 0006 (Accepted 2026-10-19) fixes two ABI classes,
erasure rules, boundary moves and versioning, and creates the normative registry
`docs/crug/ABI.md`. The registry records the C-ABI-transparent guarantees that
already hold and leaves every Crug-native layout (`slice`, `option`, `result`,
payload enum, `chan`) pending until the phase that ships it freezes it. ADR 0006
was ratified on 2026-10-19, so the policy is binding; only the Crug-native layout rows remain pending.

The FFI metadata question is now answered and binding: ADR 0007 (Accepted 2026-10-19) defines the
`.crugffi` v1 format - a versioned, line-oriented sidecar that states ownership,
borrowing, destructors, out-parameters, nullability, callbacks, blocking and
thread affinity for C functions, and nothing about layout. It cannot change
codegen, is never loaded at run time, rejects conflicts deterministically, and
under `-fcrug-strict` must be named explicitly rather than discovered. A reference
fixture, `gcc/testsuite/crug.dg/ffi/libc.crugffi`, implements the small libc
catalog of MASTER_SPEC section 16.5 minus `realloc`. The parser and the rest of
the corpus are Phase 9 work; nothing consumes the format yet.

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
| The decision set was unratified, so nothing could be cited as settled | eight ADRs carry Status Accepted (six on 2026-10-18, ADRs 0006 and 0007 on 2026-10-19); spec/000-PRECEDENCE.md Accepted (2026-10-18) |
| No kernel source was pinned, so K0 and Phase 19 were untestable | docs/crug/KERNEL-BASELINE.md (Linux 6.18.57 LTS, tag/commit/SHA256, x86_64_defconfig, qemu markers) + scripts/kernel-pin.sh; C-09 resolved 2026-10-18 |
| Exit gates were partially unfalsifiable | docs/crug/EXIT-GATES.md |
| No stable diagnostic-code registry | docs/crug/DIAGNOSTICS.md (62 planned codes) + scripts/check-diagnostics.sh |
| The kernel baseline had no CI protection, so Phase 2 could not close and Phase 4 could not start | `.github/workflows/kernel-compat.yml` green on `78c759b`; branch protection on `main` requires `kernel-compat`; phase-2 and phase-3 gates 4/4 archived under docs/crug/gates/phase-{2,3}/78c759b/ |
| No mechanism existed to evaluate a gate | scripts/gate.sh (`--list`, `--run`, archives per-check logs) |
| No upstream GCC was pinned | docs/crug/GCC-BASELINE.md + docs/crug/adr/0001-gcc-16.2-baseline.md + docs/crug/gcc-baseline.sha512 |
| The fork base lived outside git, so the tree could not be reproduced from a clone | GCC 16.2.0 imported as commit 57ccbae and tagged `releases/gcc-16.2`; `git diff releases/gcc-16.2..HEAD -- gcc/` is the fork delta |
| No way to reproduce the toolchain | Makefile (`make baseline`: verify, prerequisites, configure, all-gcc; `make build-full` adds target libraries and links; `make help`) |
| Repository licence was MIT, incompatible with the GCC fork | LICENSE (GPL-3.0-or-later), NOTICE (per-path map); C-07 resolved by the owner on 2026-10-18 |
| Line citations rotted as documents were revised | every citation now names a section or a gate phase and check (rule in spec/000-PRECEDENCE.md section 2) |
| The C testsuite could not run (no root, no DejaGnu) | `make bootstrap-dejagnu` installs DejaGnu/Expect/Tcl under `tools/`; `make check-c` runs the suite; result archived at docs/crug/baseline/phase0/ (224,481 pass, 84 environmental fail) |
| Phase 0 was unadjudicated | `scripts/gate.sh --run phase-0` passed 5/5 checks; logs under docs/crug/gates/phase-0/ |
| No driver existed, so plain-C compatibility could not be measured | `gcc/crugc` launcher (docs/crug/DRIVER.md) + `corpus/` (30 programs) + `scripts/diff-cc.sh` + `make check-C_COMPAT`; phase-1 gate archived under docs/crug/gates/phase-1/ |
| Exit gates were Proposed, so no gate definition was binding, and the phase-to-milestone mapping (C-06) was unsettled | docs/crug/EXIT-GATES.md Status Accepted (2026-10-19); C-06 settled by its section 7; amendment record in section 12 |
| There was no kernel-mode flag, corpus or local runner, so the kernel constraint was untested | `-fcrug-kernel` in `gcc/common.opt` (Common Ignore); 18-source corpus under `gcc/testsuite/crug.dg/kernel/`; `scripts/test-kernel-compat.sh` (21/21), `scripts/check-kernel-objects.sh`, `scripts/check-kernel-freestanding.sh`; `make check-KERNEL_COMPAT`; phase-2 gate archived for checks 1-3 |
| Nothing would have protected the kernel baseline in CI | `.github/workflows/kernel-compat.yml` (builds the pinned baseline, runs the suite, 300-minute cap, frees disk first) green on `78c759b` + `scripts/check-ci-gate.sh` (asserts the check concluded success and is a required status check) |
| No C ABI policy existed, so qualifier erasure, Crug-native type layouts and boundary moves were unsettled | docs/crug/adr/0006-c-abi-policy.md (two ABI classes, erasure, boundary moves, targets, versioning, test obligations) |
| No single place recorded ABI facts, while the Phase 10 gate already required one | docs/crug/ABI.md: C-ABI-transparent guarantees recorded, Crug-native rows pending, registry rules and layout-assertion obligation |
| The C FFI sidecar was illustrative only, so Level 2 ownership had no decidable format | docs/crug/adr/0007-ffi-metadata-format.md (v1 grammar, clause semantics, discovery, versioning, trust model) + reference fixture gcc/testsuite/crug.dg/ffi/libc.crugffi |
| The Phase 9 gate invoked the sidecar flag in a form the format does not define | docs/crug/EXIT-GATES.md phase 9 checks 4-5 now use `-fcrug-ffi=<path>` (amendment A-3) |
| No feature flag existed, so no later phase could be gated on one and no build could say which Crug features it wanted | ten `-fcrug-*` options in `gcc/common.opt` with closure in `gcc/c-family/crug-common.cc`; `docs/crug/FLAGS.md`; `scripts/check-crug-flags.sh` (149 work items), `scripts/check-gnu-c-compat.sh` (50 files), `scripts/diff-obj.sh`; fixtures in `gcc/testsuite/crug.dg/flags/` |
| Predefined feature macros contradicted the flags-off identity guarantee | ADR 0018 D9 R6 amendment A-1 and spec/000-PRECEDENCE.md C-12: `__CRUG__` and the feature macros exist only when a `-fcrug-*` option is active |

## Still open

| ID | Item | Blocked on |
|----|------|------------|
| C-05 | P1 conformance refers to a release matrix that does not exist | RFC-0010 |
| C-08 | "Strict mode" used but never defined | defined operationally by ADR 0018 D1/D10; needs a consolidated paragraph in docs/crug/STRICT.md |
| -  | The Phase-19 per-object corpus list `kernel/KERNEL-CORPUS.md` does not exist; the kernel source is pinned but the compile corpus is not | Phase 19 gate |
| -  | Phase-20 golden corpus (50 error classes with stable wording) is not written | Crug frontend; docs/crug/DIAGNOSTICS.md seeds the classes |
| -  | No `.crugffi` parser exists and only one fixture is written; the Phase 9 corpus in ADR 0007 D11 is otherwise unwritten | Phase 9 implementation |
| -  | Every Crug-native layout is a pending row in `docs/crug/ABI.md`; no size, alignment or calling representation is frozen | Phases 10 and 13-14 (slice/option/payload enum, then chan); `result` also needs its focused RFC |
| -  | No Crug production exists: the flags are accepted but nothing parses `own`, `move`, `borrow` or the rest, and no `-Wcrug-*` option is declared | Phase 4 (ownership MVP) onward |
| -  | The version macros `__CRUG_VERSION_*`, `__CRUG_RUNTIME_ABI__` and `__CRUG_FFI_VERSION__` asked for by RFC-0001 section 10 are not defined | ADR 0013 (versioning); the feature macros exist today |

## Next actions, in dependency order

1. **Start Phase 4** (ownership MVP: `own` and `move`): the flags and the
   contextual-word infrastructure are in place, and ADRs 0003 and 0004 are binding
   on where ownership lives and what a move may do. Phase 5 (drop) follows Phase 4,
   and Phase 9's FFI parser comes after Phases 5 and 8.
2. **Write the Phase-19 kernel compile corpus** (`kernel/KERNEL-CORPUS.md`) on top
   of the pinned Linux 6.18.57 tree.

## Working rules

- PRECEDENCE.md section 2 decides any document conflict.
- A semantic change needs an ADR plus tests in `gcc/testsuite/crug.dg/`.
- No phase gate is skipped; EXIT-GATES.md is the definition of "done", and a claim
  without an archived `scripts/gate.sh` run does not count.
