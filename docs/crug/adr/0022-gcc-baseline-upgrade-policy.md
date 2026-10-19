# ADR 0022 - GCC baseline upgrade policy

- **Status:** Proposed
- **Date:** 2026-10-19
- **Deciders:** project owner (ratification of a major upgrade), compiler
  maintainer (execution and patch/minor upgrades)
- **Relates:** ADR 0001 (the baseline pin), `docs/crug/GCC-UPGRADE.md` (the
  measured procedure and the acceptance matrix), `docs/crug/EXIT-GATES.md`
  phase 0 (the pin checks)

## Context

ADR 0001 pins GCC 16.2.0 and states that "replacing the baseline requires a new
ADR, not an edit"; its open question 3 adds that point releases from 16.3 on need
a new ADR too. That rule is simultaneously too heavy and too vague: it turns a
provenance change into a decision record, and it says nothing about what evidence
an upgrade must produce.

The fork has since been measured against upstream (`docs/crug/GCC-UPGRADE.md`):

- 9 upstream files with 32 hunks (+495/-6) are the whole conflict surface;
- 7 Crug-only sources (5,021 lines) and the testsuite never conflict;
- against GCC master on 2026-10-19, 31 of 32 hunks apply with identical context,
  the one drift is mechanical, all 23 attachment symbols exist and all 8
  behavioural pins hold.

So an upgrade is a rebase plus a verification exercise. The policy therefore needs
to distinguish a mechanical re-pin from a change that can alter behaviour, and it
needs to say exactly which evidence closes each class. It also must protect the
property that makes the fork reviewable: a gate archive is evidence for one
compiler version at one commit, and an upgrade invalidates every archive it is
compared against.

## Decision

### D1 - An upgrade is a build-input change, not a language change

A baseline upgrade replaces the pinned tarball, its digest, the vendor import
commit and its tag. It changes no Crug rule by itself. Any behaviour change it
introduces must be attributable to upstream, and is fixed or recorded, never
waved through as "just a bump".

### D2 - Three classes, three evidence levels

| Class | Example | Decision record | Evidence |
|-------|---------|-----------------|----------|
| P, re-pin | same 16.2.0 release from a re-signed tarball or a mirror | none; the pin commit quotes old and new digests | `make verify` and a rebuild that reports the same version |
| M, minor | 16.2 -> 16.3 (same major) | an amendment row in ADR 0001 and a `GCC-BASELINE.md` changelog row | the full acceptance matrix (D5) |
| J, major | 16.x -> 17.x | a new ADR superseding ADR 0001 | the full acceptance matrix (D5) plus owner ratification |

A P-class change must not alter `xgcc --version`; if it does, it is an M-class
change. A change that alters the C front end's behaviour for existing Crug syntax
is never P-class, whatever the version string says.

### D3 - The old baseline must be green first

No upgrade starts until `scripts/verify-upgrade.sh` passes 31/31 on the commit
being upgraded. Without a green starting point, every new failure is unattributable
and the review degenerates into guesswork.

### D4 - The procedure is fixed

The seven steps in `docs/crug/GCC-UPGRADE.md` section 4 are the procedure:
decide the class and branch, pin, import and tag, replay with
`git rebase --onto`, build, re-baseline and verify, record and push. A deviation
must be explained in the upgrade commit message; an unexplained deviation is a
review defect.

### D5 - Acceptance is the matrix at a pushed commit

An M- or J-class upgrade is accepted only when all of the following hold at the
upgrade commit:

1. `scripts/verify-upgrade.sh` reports 31 passed, 0 failed;
2. the phase gates 0-9 pass at that commit and their logs are archived under
   `docs/crug/gates/`;
3. the `gcc.dg` baseline is re-recorded under `docs/crug/baseline/phase0/` and
   every new `FAIL` is either fixed or added to `known-fail.txt` with a reason;
4. `scripts/check-gcc-drift.sh` against the new vendor tag exits 0;
5. the commit is pushed and the `kernel-compat` check is green on it.

A local-only run closes nothing. This is the same rule the phase gates already
apply, extended to the compiler itself.

### D6 - Unknown drift blocks the merge

`scripts/check-gcc-drift.sh` exiting 1 blocks the merge. The only way past is a
row in `docs/crug/gcc-drift-known.txt` that names the path, the exact context line
that moved and a reason. Exit 2 (network, unknown revision) is not a pass and
must not be treated as one.

### D7 - Branch, tag and history discipline

Work happens on `gcc-upgrade/X.Y.Z`. The imported tree is committed on that
branch and tagged `releases/gcc-X.Y.Z` (annotated). A vendor tag is never moved
or deleted, and `main` receives the upgrade only after D5. The previous vendor
tag is retained so the old base stays reproducible.

### D8 - Version claims must agree with the build

`docs/crug/GCC-BASELINE.md` and the `Makefile` pin are the only places a version
is recorded, and `make xgcc-version` must print the same version. A tree that
builds "xgcc (GCC) 16.2.0" may not be described as 16.3 anywhere, including in
the testsuite baseline README and the gate logs.

### D9 - Who ratifies what

P- and M-class upgrades are executed by the maintainer and recorded, because they
are mechanical and reversible by the same procedure. A J-class upgrade requires
owner ratification of the superseding ADR before the merge. The monthly
`gcc-drift` workflow is advisory: it reports, it does not decide.

### D10 - Forbidden

- Porting the fork to upstream trunk as the primary base. The baseline is a
  release; trunk has no stable reference to cite.
- Cherry-picking an upstream front-end fix into the fork instead of taking a
  baseline that contains it, except as an explicitly recorded temporary.
- Editing a digest without re-verifying it against the published checksum.
- Adding a test to `known-fail.txt` without a reason.
- Moving, deleting or reusing a vendor tag.
- Claiming an upgrade from a local run, or from a run at a different commit.
- Treating a drifted hunk that happens to apply as "unchanged" without checking
  its behaviour (pins P1-P8 exist for exactly that).

## Consequences

- Patch and minor upgrades become routine, about one day of mostly machine time,
  and require no new decision record; major upgrades stay explicit and owner-owned.
- Gate archives stay attributable: they name a commit and a compiler version, and
  a new baseline always produces new evidence.
- The cost is real: every upgrade re-runs a two-hour testsuite and the phase gates.
  That cost is the price of the ABI and zero-cost claims being measured rather
  than asserted.
- The matrix becomes the gate for the compiler itself, so a change that breaks it
  cannot ship as a "just a bump" commit.

## Alternatives rejected

- **A new ADR for every bump.** Paperwork without information; ADR 0001 would be
  rewritten on every patch release and the decision record would lose its meaning.
- **Treat the baseline as an ordinary dependency.** The C ABI and codegen identity
  claims would become unverifiable, because nothing would tie them to a compiler.
- **Track upstream master.** Unstable base, no release to cite, and the release
  gate model assumes a fixed reference.
- **Track a vendor release branch instead of a tarball.** Still needs the same
  replay, adds divergence, and weakens the checksum anchor.

## Test obligations

- `docs/crug/GCC-UPGRADE.md` records the measured drift and the procedure;
  `scripts/fork-delta.sh`, `scripts/check-gcc-drift.sh` and
  `scripts/verify-upgrade.sh` implement the checks named here.
- Phase 0's checks read the pin file, so a version change flows through the pin
  rather than through edited gate text.
- The upgrade is tested by D5's matrix; a future upgrade is the drill.
- The drift registry is tested by `check-gcc-drift.sh` refusing unknown drift.

## Open questions

1. Should a dry run against a release candidate be scheduled to de-risk a major
   jump (e.g. 17.0-RC1) instead of waiting for the final release?
2. Does the owner want the J-class ADR ratified before the branch is proven green,
   or after, with the green branch as the argument for ratification?
3. How many old vendor tags should be kept? History is cheap, but the repository
   is already large.
4. Should the monthly drift job open an issue automatically when drift appears,
   or only publish a failing run?
