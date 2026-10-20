# Upgrading the GCC baseline

Status: assessment complete on 2026-10-19; the measurement is reproducible with
`scripts/fork-delta.sh` and `scripts/check-gcc-drift.sh`. The policy lives in
ADR 0022 (Proposed). No version change has been made: the pinned baseline is
still GCC 16.2.0.

## 1. Short answer

Moving to a newer GCC is a **rebase of a small patch**, not a rewrite, as long as
GCC's C front end keeps its shape:

- The fork modifies **9 upstream files with 32 hunks** (+495/-6 lines) as measured on 2026-10-19; the 2026-10-21 re-measurement is **10 files with 63 hunks** (+2,812/-41, section 2.3). Those are
  the only files that can produce rebase conflicts.
- Everything else Crug adds lives in new sources that no GCC tree contains: **7
  files (5,021 lines)** on 2026-10-19, **17 files (8,989 lines)** at the
  2026-10-21 re-measurement (section 2.3), plus the testsuite, docs, scripts and
  examples; a new GCC tree does not touch those files, so they replay without
  conflicts.
- Measured against GCC master on 2026-10-19: **31 of 32 hunks apply with
  byte-identical context**, the single drift is a mechanical
  `BOOL_BITFIELD` -> `bool` change in `struct c_declspecs`, and all 23
  attachment symbols and all 8 behavioural pins still exist.

Consequences:

| Change | Typical work | Wall clock | Confidence |
|--------|--------------|-----------|------------|
| Patch release inside the same major (e.g. 16.2 -> 16.3) | resolve 0-2 hunks, update pins, re-baseline | about one day, mostly machine time | high |
| Next major (17.0/17.1) | resolve a handful of hunks, re-check the pins | 2-5 working days typical; 1-2 weeks if the C front end is refactored | medium |
| A refactor of the declaration parser or of `struct c_declspecs` | adapt the hooks, not the 5,000 lines | the upper end of the range above | low until it happens |

The expensive part is **verification**, not porting: a full ```make check-c``` run
takes about two hours, the phase gates about an hour, and a from-scratch build
30-60 minutes. The ~9,000 lines of Crug code use ordinary tree/parser APIs and do
not need to be rewritten for a GCC version change.

## 2. The two deltas

`scripts/fork-delta.sh` prints both halves. On 2026-10-19, at commit a4ebd41:

### 2.1 Files upstream also owns (replayed on upgrade)

| File | Hunks | Insertions | Deletions |
|------|------:|-----------:|----------:|
| `gcc/Makefile.in` | 1 | 1 | 1 |
| `gcc/c-family/c-cppbuiltin.cc` | 2 | 33 | 0 |
| `gcc/c-family/c-opts.cc` | 3 | 21 | 0 |
| `gcc/c-family/c.opt` | 1 | 41 | 0 |
| `gcc/c/Make-lang.in` | 1 | 1 | 1 |
| `gcc/c/c-decl.cc` | 6 | 84 | 0 |
| `gcc/c/c-parser.cc` | 15 | 248 | 4 |
| `gcc/c/c-tree.h` | 2 | 14 | 0 |
| `gcc/common.opt` | 1 | 52 | 0 |
| **Total** | **32** | **495** | **6** |

Two of the hunks are trivial build plumbing (`Makefile.in`,
`c/Make-lang.in`); the rest are options and parser/declaration hooks.

### 2.2 Files only Crug has (never conflict)

| File | Lines |
|------|------:|
| `gcc/c/crug-ownership.cc` | 3,465 |
| `gcc/c/crug-ownership.h` | 102 |
| `gcc/c-family/crug-ffi.cc` | 1,202 |
| `gcc/c-family/crug-ffi.h` | 58 |
| `gcc/c-family/crug-common.cc` | 99 |
| `gcc/c-family/crug-common.h` | 56 |
| `gcc/crugc` | 39 |
| **Total** | **5,021** |

Plus **175 new testsuite files** under `gcc/testsuite/crug.dg/` and everything
outside `gcc/` (docs, spec, scripts, examples, corpus). Those move with the
fork and never conflict.

### 2.3 The 2026-10-21 re-measurement

Phases 10-22 added code to the upstream-owned files, so the numbers in 2.1 and
2.2 describe the tree at commit a4ebd41 (2026-10-19), not the current one.
`scripts/fork-delta.sh` at `7b7f8db` measures **10 upstream files, 63 hunks,
+2,812/-41** and **17 new sources, 8,989 lines** plus 405 testsuite files. The
extra upstream file is `gcc/diagnostics/sarif-sink.cc`, which Phase 20 changes so
a Crug rule's code becomes the SARIF `ruleId`. The qualitative claims above
stand: the delta is still textual insertion into a small set of front-end
files. The exact hunk counts, the 31-of-32 replay figure and the effort
estimates must be re-measured with `scripts/verify-upgrade.sh` before a re-pin,
which is what ADR 0022 requires anyway. This section records the drift instead
of rewriting section 2.1, so the original study stays auditable.

## 3. Porting map

The integration is **textual insertion into front-end functions**, not a GCC
plugin. That is a deliberate choice (a plugin cannot add declaration specifiers
or own the parser), and it means an upgrade is only as hard as the functions
below are stable.

### 3.1 Attachment points

| File | Anchor | What the fork does there | Internals relied on | Stability |
|------|--------|--------------------------|---------------------|-----------|
| `gcc/c/c-parser.cc` | file headers | includes the Crug headers | normal include order (config.h first) | stable |
| `gcc/c/c-parser.cc` | `c_parser_peek_nth_token` | qualifier lookahead helpers (own/ref/mut/raw) | `c_token`, `CPP_NAME`/`C_ID_ID`, `lookup_name` | medium |
| `gcc/c/c-parser.cc` | `c_parser_next_token_starts_declspecs` | an undeclared qualifier starts a declaration | token predicates | medium |
| `gcc/c/c-parser.cc` | `c_parser_next_tokens_start_declaration` | recurse past a qualifier so `own Foo *p;` parses as a statement | same predicates | medium |
| `gcc/c/c-parser.cc` | `c_parser_declaration_or_fndef` (2 hunks) | unknown-type heuristic; run the analysis after the body is parsed | `current_function_decl`, the body `BIND_EXPR` | medium |
| `gcc/c/c-parser.cc` | `c_parser_declspecs` | consume a qualifier into `struct c_declspecs` | `struct c_declspecs` | medium |
| `gcc/c/c-parser.cc` | `c_parser_statement_after_labels` | `unsafe` blocks, `scope`, statement-position intrinsics | `c_parser_compound_statement`, `add_stmt` | medium |
| `gcc/c/c-parser.cc` | `c_parser_cast_expression` (2 hunks) | strict-mode cast policy | result type of `c_cast_expr` | medium |
| `gcc/c/c-parser.cc` | `c_parser_sizeof_or_countof_expression`, `c_parser_alignof_expression` | accept a qualifier in a type-name | type-name parsing | medium |
| `gcc/c/c-parser.cc` | `c_parser_postfix_expression` | build the intrinsic calls (`move`, `adopt`, `release`, `drop`, `borrow`, `borrow_mut`) and mark operands read | `C_ID_ID`, `build_call_expr_loc`, `mark_exp_read` | medium |
| `gcc/c/c-parser.cc` | `c_parse_file` | finish and validate the `.crugffi` metadata | file-scope `lookup_name` still valid at this point | **fragile** (pin P4) |
| `gcc/c/c-decl.cc` | `start_decl` | mark owning, borrow and raw declarations | `declspecs`, `DECL_UID` | medium |
| `gcc/c/c-decl.cc` | `push_parm_decl` | mark `own`/`raw` parameters | parameter parsing | medium |
| `gcc/c/c-decl.cc` | `grokfield` | member qualifiers; reject bit-field/flexible-array/union combinations | struct-field parsing | medium |
| `gcc/c/c-decl.cc` | `finish_struct` | own-member policy | struct completion | medium |
| `gcc/c/c-decl.cc` | `start_function` | reject a function returning a borrow | function definitions | medium |
| `gcc/c/c-tree.h` | `enum c_declspec_il` / `struct c_declspecs` | qualifier kind; `crug_qual`/`crug_own_p` fields | struct layout and bitfield style | **fragile** (seen drifting already) |
| `gcc/c-family/c-opts.cc` | `c_common_handle_option` | intercept `-fcrug-ffi=` | `CL_C` option dispatch | stable |
| `gcc/c-family/c-opts.cc` | `c_common_post_options` | apply the feature closure; set up FFI metadata | option closure point | stable |
| `gcc/c-family/c-cppbuiltin.cc` | `c_cpp_builtins` | define the feature and version macros | predefined-macro hook | stable |
| `gcc/c-family/c.opt`, `gcc/common.opt` | option tables | declare `-fcrug-*` and `-Wcrug-*` | option file syntax | stable |
| `gcc/Makefile.in`, `gcc/c/Make-lang.in` | object lists | link the Crug objects | object-list variables | stable |

Rule of thumb: the **stable** rows need no work on a version bump; the **medium**
rows are where a rebase conflict may appear; the two **fragile** rows are where a
version bump can change meaning even when the patch applies cleanly.

### 3.2 Behavioural pins

These are not symbols; they are assumptions about behaviour that the fork needs
and that a compiler upgrade can silently change. `scripts/check-gcc-drift.sh`
checks all of them.

| Pin | Assumption | Why it matters | If it breaks |
|-----|-----------|----------------|--------------|
| P1 | `libcpp/directives.cc` calls `pfile->cb.include` with `(pfile, line, directive_name, fname, angle, buf)` and the third argument is the **directive name**, not a directory | the `.crugffi` discovery hook derives the includer from `LOCATION_FILE (line)` | header-adjacent discovery silently stops finding sidecars |
| P2 | `c_build_bind_expr` returns the bare body when a compound statement declares nothing | `unsafe { ... }` must survive into the analysis as a marked `BIND_EXPR` | unsafe blocks stop suppressing checks |
| P3 | `BIND_EXPR_VARS` is a `DECL_CHAIN` chain, not a `TREE_LIST` | the return-value temporary is prepended to the body's vars | ICE or wrong destruction order |
| P4 | file-scope `lookup_name` still sees declarations at the end of `c_parse_file` | `.crugffi` entries are validated against the translation unit | every sidecar entry reported undeclared |
| P5 | a `Common` option with no `Var` asserts in `gcc/opts.cc` (`option_flag_var`) | `-fcrug-ffi=` must be a C-language option so `c_common_handle_option` sees it | ICE in `common_handle_option` (observed) |
| P6 | `aux_base_name` names dumps; `mark_exp_read` is what `-Wunused-but-set-variable` consults | dump file naming; no false unused warnings for intrinsic operands | dumps in the wrong place; spurious warnings |
| P7 | GCC requires an **ISO C++14** host compiler | the build host must satisfy it before the fork builds at all | configure fails early |
| P8 | GCC 16 has no `EXPR_STMT`/`STMT_EXPR` tree codes | the analysis handles statement lists directly | dead code in the pass compiles as a reference to a missing enum |

P1 and P5 were discovered the hard way while building the fork; they are exactly
the kind of fact a rebase must not rediscover.

## 4. Upgrade procedure

Every step is a commit on a branch named `gcc-upgrade/X.Y.Z`; main is only
updated when the acceptance matrix passes.

1. **Decide the class and open the branch.** ADR 0022 classifies the change:
   patch, minor or major. Create `gcc-upgrade/X.Y.Z` from main.

2. **Pin the new release.** Download the release tarball, compute its SHA512,
   and update `docs/crug/gcc-baseline.sha512`, `GCC_VERSION`/`GCC_URL` in
   the `Makefile`, the `docs/crug/GCC-BASELINE.md` pin table and (per class)
   ADR 0001 or its successor. Commit as `crug: build: pin GCC X.Y.Z`.

3. **Import the pristine tree.** Extract the tarball under `build/import/`,
   copy it over the repository root, and keep the Crug-only paths listed in
   section 2.2 (plus `docs/`, `spec/`, `scripts/`, `examples/`,
   `corpus/`, `gcc/testsuite/crug.dg/`, `Makefile`, `README.md`,
   `NOTICE`, `LICENSE`, ```.gitignore```, ```.github/```). Merge the root
   ```.gitignore``` with the upstream one, remove the upstream files the new
   release deleted, and stage everything with `git add -f --pathspec-from-file`
   from a fresh `find` manifest. Commit as
   `crug: gcc: import the pinned GCC X.Y.Z tree as the fork base` and tag it
   `releases/gcc-X.Y.Z` (annotated). Never retag.

4. **Replay the fork.** `git rebase --onto <import-commit> releases/gcc-16.2 main`.
   Only the nine files in section 2.1 can conflict; resolve them with the porting
   map in section 3, then finish the rebase. Confirm the replay with
   `scripts/fork-delta.sh --tag releases/gcc-X.Y.Z`: the hunk and line count of
   the nine files should match the old delta, plus any new work.

5. **Build from the in-repository tree.** `rm -rf build/obj` then `make
   baseline`. The import means the `extract` step is a no-op and the build
   reads the repository root. `make build-full` if the tests need target
   libraries.

6. **Re-baseline and verify.**
   - `make baseline-phase0` re-runs `gcc.dg` (about two hours) and archives
     the result under `docs/crug/baseline/phase0/`.
   - Review `known-fail.txt`: every new `FAIL` must be either fixed in the
     fork or added to the allowlist **with a reason**. A new failure is a real
     regression until proven otherwise.
   - `scripts/verify-upgrade.sh` runs the 31-check matrix (section 5).
   - `scripts/verify-upgrade.sh --gates` additionally runs the phase gates 0-9
     and archives their evidence exactly as a release adjudication does.
   - `scripts/check-gcc-drift.sh` against the new vendor tag reports the drift
     that is left between the replayed fork and the newer upstream.

7. **Record and push.** Update `docs/crug/progress.md`, `GCC-BASELINE.md` and
   the ADR set; commit the gate and baseline evidence; push so the
   `kernel-compat` CI job runs on the new baseline. The upgrade is not done
   until the gates pass at a pushed commit, not merely at a local one.

## 5. Acceptance matrix

`scripts/verify-upgrade.sh` is the single entry point. It runs 31 checks in
four groups and prints one `PASS`/`FAIL` line each, keeping the full output
under `build/verify-upgrade/`.

| Group | Checks |
|-------|--------|
| Pins and baseline | version pin matches the build; the archived `gcc.dg` baseline was recorded with the same compiler |
| C and kernel compatibility | C corpus across three option matrices; GNU C and kernel corpora in hybrid mode; the kernel compatibility suite |
| Ownership, drop, borrow, strict | the four DejaGnu suites plus codegen identity, exactly-once, ASan, objdump, policy, dump and the real-C programs |
| FFI and registry | contract suite, integration with the four real libraries, deterministic conflict, codegen identity, policy codes; the diagnostic registry |

A recorded run on 2026-10-19 at commit a4ebd41: **31 passed, 0 failed** with
`xgcc (GCC) 16.2.0`. `--gates` adds phases 0-9.

The matrix is not a rubber stamp: writing it exposed three stale checks that the
phase gates had stopped covering - the GNU C corpus was still compiled with
`-fcrug-modern` after strict mode began rejecting unannotated `asm` and
`setjmp`; the borrow ABI twin passed a borrow to a function with no borrow
contract, which the depth-zero policy rejects; and the version-pin reader did not
match the Makefile's aligned `?=`. All three are fixed.

## 6. Effort model

| Work item | Patch (16.x) | Major (17.x) |
|-----------|-------------|--------------|
| Pin, import, rebase | 1-2 hours | 1 day, plus conflicts |
| Conflict resolution on the 9 files | usually none | a few hours to 2 days |
| Build (20 cores / CI 4 cores) | 30-60 min / ~16 min | same |
| `gcc.dg` re-baseline | ~2 hours | ~2 hours, plus allowlist review |
| Local matrix (31 checks) | ~5 minutes | ~5 minutes |
| Phase gates 0-9 | ~1 hour | ~1 hour, plus fixes |
| Docs, ADR, evidence | 1-2 hours | half a day |
| **Wall clock** | **about one day** | **2-5 days typical, 1-2 weeks worst case** |

The release state on 2026-10-19: GCC 16.2 (2026-08-07) is the newest release;
15.3 (2026-06-12) and 16.1 (2026-04-30) are older; **no 16.3 exists yet**, and
GCC 17 is unreleased. The next realistic move is either a 16.2.x/16.3 patch
release or 17.0 once it ships; the measured drift says neither needs a rewrite.

## 7. Risk register

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| R1 The C parser is refactored (splitting `c_parser_declaration_or_fndef` or `c_parser_declspecs`) | medium over a major | high: hook sites must be re-found | `fork-delta.sh` shows the hunk set; the porting map says what each hook needs |
| R2 `struct c_declspecs` changes layout or bitfield style | already happened once | low-medium: mechanical port | drift check lists the exact missing context lines |
| R3 `libcpp` include-callback or `.crugffi` discovery contract changes | low | high: FFI discovery silently degrades | pin P1 is checked by `check-gcc-drift.sh`; the FFI suite includes an adjacent-include case |
| R4 The option system changes (option flags, enum names, dispatch) | low | medium: flags stop being accepted | flag suite plus `crug-flags` checks, `OPT_fcrug_ffi_` naming |
| R5 The DejaGnu harness or `dg-` directive behaviour changes | medium | medium: suite results shift | re-baseline and review `known-fail.txt`; never add to the allowlist without a reason |
| R6 Build host requirements move (C++17 compiler, newer GMP) | medium over years | low: environment work | `GCC-BASELINE.md` prerequisites table |
| R7 Upstream files removed or renamed | medium | low: import step handles it | manifest and `git status` in the import commit |
| R8 A new upstream warning becomes an error in the fork's own code | low | low | rebuild and fix; Crug code is small |

## 8. What would make it expensive

- A refactor of the declaration parser or of `struct c_declspecs`, because the
  nine hooks are textual insertions rather than a stable interface.
- A change to `libcpp`'s include callback contract, because `.crugffi`
  discovery depends on it.
- Moving the fork to a C++ standard the build host cannot satisfy.
- A large testsuite churn (new expected diagnostics, dg-directive changes) that
  makes the baseline archive noisy and the allowlist hard to review.
- Anything that changes the tree codes the analysis switches on; the pass handles
  the C front end's GENERIC directly.

None of those is the 5,000 lines of Crug code itself: that code uses the tree and
parser APIs that GCC keeps stable for years.

## 9. Reproducing the measurement

```sh
scripts/fork-delta.sh                  # offline: the 9 files, 32 hunks, 7 new sources
scripts/check-gcc-drift.sh             # upstream master: contexts, symbols, pins
scripts/check-gcc-drift.sh <sha>       # a specific upstream revision
CRUG_VENDOR_TAG=releases/gcc-17.1 scripts/fork-delta.sh
```

`check-gcc-drift.sh` resolves the upstream revision with
`git ls-remote`, fetches only the files under test into
`build/gcc-drift/<sha>/`, replays every hunk context line, checks the 23
symbols and the 8 pins, and exits 0 only when the remaining drift is recorded in
`docs/crug/gcc-drift-known.txt` with a reason. Exit 1 means unknown drift;
exit 2 means the network or the repository could not be read. A monthly
`gcc-drift` workflow runs the same check on GitHub, and a run costs nothing
when upstream has not moved.

Measured on 2026-10-19 against `gcc-mirror/gcc@c308d38b0849` (the master
branch at 05:04 UTC):

```
drift-summary: 32 hunks on 9 files, 31 clean, 1 drifted (2 known context
line(s), 0 unknown); symbols 23/23; pins 8/8
```

## 10. Open questions

1. Patch-release policy is written in ADR 0022 but not yet ratified; until then
   the "no new ADR for a patch release" rule is a proposal, not law.
2. Should the fork track upstream master continuously (the monthly drift job
   already reports it) or only released versions? The policy currently says
   released versions only.
3. Should any of the nine hooks be offered upstream? A `-fplugin`-style
   interface would be a different design; not planned.
4. When 17.0 ships, port immediately or wait for 17.1 to absorb the early
   breakage? The process supports either; the gates decide.
5. How long is the 16.2 vendor tag kept after a move? The import history is
   cheap to keep and the `releases/gcc-*` tags are never rewritten.
