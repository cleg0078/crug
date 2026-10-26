# Testing the Crug compiler

Status: **Phases 0-22 implemented and measured, 2026-10-20.** Counts on this page were observed on the reference host (Linux x86-64, 20 logical CPUs) or read from `docs/crug/gates/`; the archive, not this page, is the evidence.

## 1. The test layers

A change is proven at the layer that can observe it; a phase gate names the layers it needs.

| Layer | Lives in | Run with | Proves |
|-------|----------|----------|--------|
| Upstream C baseline | `gcc/testsuite/gcc.dg` and the rest of the upstream suite | `make check-c`, `make baseline-phase0` | Plain C has not regressed: 224,481 PASS, 84 FAIL, 23 XPASS on the phase-0 record, with the known-fail list explaining every FAIL. |
| Crug DejaGnu suites | `gcc/testsuite/crug.dg` | `make check-<SUITE>` | Acceptance, rejection, warning wording, dumps, ABI and layout via dg directives: 10 `.exp` drivers over 321 `.c` fixtures. |
| Script suites | `scripts/check-*.sh` (56 of 71 scripts) | `make check-<SUITE>` | Exact tallies, symbol absence, object identity, dump shape, multi-OS and kernel observables that dg cannot express. |
| Runtime programs | `libcrug/tests` (28 `.c` files) | `make check-GOROUTINES`, `check-SCOPE`, `check-CHANNELS`, `check-SEND_SYNC` | Scheduler, scope, channel and PAL semantics, including 100,000-task and scope stress runs. |
| Kernel suite | `kernel/` | `make check-KERNEL` | K1-K7: header probes, 12 TUs, `lib/`, the 438-object corpus, a full `vmlinux`/`bzImage`, a qemu boot smoke and the aarch64 subset. |
| Fuzz | `fuzz/compiler`, `fuzz/runtime` | `make check-FUZZ` | Six harnesses (four compiler, two runtime) over committed seeds with zero crashes, timeouts or new findings. |
| Sanitizer | `scripts/check-sanitizer.sh` | `make check-SANITIZER` | ASan and UBSan clean over tasks, scopes, channels and the OS-thread stress; TSan clean where no fiber switch happens. |
| Performance | `bench/` | `bench/run.py --performance --check` | Alias-fact equivalence (5%), plain-C parity (2%), zero-cost IR/assembly, pristine object parity, concurrency and kernel codegen. |
| Documentation | `docs/crug/MIGRATION-EXAMPLES.md`, `examples/` | `make check-docs-examples` | Every marked migration example prints its recorded stdout. |
| Gate adjudicator | `scripts/gate.sh`, `docs/crug/gates/` | `scripts/gate.sh --run phase-N` | The CHECK lines of `docs/crug/EXIT-GATES.md` run verbatim and their observables are archived. |

The fixtures group by feature: borrow 52, safe-types 42, drop 27, ffi 27, channels 24, kernel 18, goroutines 17, strict 15, scope 14, ownership 12, send-sync 10, alias 9, flags 3, optimization 1, diagnostics 50. Every suite prints one tally line that `docs/crug/EXIT-GATES.md` quotes and exits nonzero on failure; runs are deterministic, tests never write the source tree, and no suite needs the network once the one-time DejaGnu bootstrap (section 2) has run.

## 2. Running the suites

Build the pinned baseline first: `make baseline JOBS=$(nproc)` configures and builds `build/obj` from the pinned `releases/gcc-16.2` source. The `gcc/crugc` wrapper resolves `CRUGC_REAL_DRIVER`, `build/obj/gcc/xgcc`, `gcc/xgcc`, `gcc/gcc` in that order; check `gcc/crugc --version` before blaming a test. A full build takes 30-60 minutes, upstream `make check-c` about two hours on the reference host; Crug suites are minutes each.

DejaGnu is bootstrapped, not installed: there is no `runtest` and no sudo. Each suite target starts with `if ! command -v runtest; then scripts/bootstrap-dejagnu.sh; fi`, sources `tools/dejagnu.env`, then runs `$(MAKE) -C build/obj/gcc check-c RUNTESTFLAGS='crug.dg/<dir>/<dir>.exp'` through `tee` and a `grep` filter. The bootstrap downloads `tcl8.6 libtcl8.6 tcl tcl-expect expect dejagnu` with `apt-get download` into `tools/debs`, unpacks under `tools/root` with `dpkg -x`, repairs the `tclsh` symlink and writes `tools/dejagnu.env`. `tools/` is ignored and never committed.

`make check` filters its log and ends in `|| true`, so it reports failures without failing; `make check-c` and `make baseline-phase0` propagate the real status, so any script that must fail on an upstream FAIL uses `check-c`.

| Target | Fixtures or subject | Observable |
|--------|--------------------|------------|
| `make check-OWNERSHIP` | `crug.dg/ownership` (12) | dg acceptance and rejection cases naming their `[crug-UAM-*]` or `[crug-OWN-*]` code. |
| `make check-DROP` | `crug.dg/drop` (27) | dg cases plus exactly-once 4/4, drop-asan 3/3, drop-objdump 5 objects with no `__crug_` calls, policy and the five exit kinds in the dump. |
| `make check-BORROW` | `crug.dg/borrow` (52) | dg cases plus borrow cost, ABI, policy and dump scripts. |
| `make check-STRICT_UNSAFE` | `crug.dg/strict` (15) | Hybrid code exits 0, strict code is diagnosed, codegen parity over four programs. |
| `make check-FFI` | `crug.dg/ffi` (27) | The case count with zero failures, integration, conflict stability, codegen and policy codes. |
| `make check-SAFE_TYPES` | `crug.dg/safe-types` (42) | The tally, bounds behaviour and owned-payload checks. |
| `make check-GOROUTINES` | `crug.dg/goroutines` (17) | Compiler cases, five task programs and the 100,000-task stress with zero failures. |
| `make check-SCOPE` | `crug.dg/scope` (14) | Scope cases, group programs, zero leaked groups and the two scope diagnostics. |
| `make check-CHANNELS` | `crug.dg/channels` (24) | Channel cases, seven channel programs and the send-owned/select-own-recv rejections. |
| `make check-SEND_SYNC` | `crug.dg/send-sync` (10) | The case count plus raw pointer, thread affinity and FFI annotation rejections. |
| `make check-C_COMPAT`, `check-GNU_C_COMPAT` | Option matrices and 50 GNU C files | Three matrices plus two checks, zero FAIL; 50 GNU C files, zero FAIL. |
| `make check-CRUG_FLAGS` | `crug.dg/flags` (3) and flag scripts | Every flag alone, macro closure, accepted pairs, the three kernel conflicts, unknown-option rejection. |
| `make check-MULTI_OS` | `scripts/check-multi-os-ports.sh` | One line naming platform and counts; the cross arm runs aarch64 under qemu. |
| `make check-KERNEL` | `kernel/` | K1-K7 stage lines ending with the boot smoke; the object diff reports 436 identical and 2 explained. |
| `make check-KERNEL_COMPAT` | `scripts/test-kernel-compat.sh` | 21 work items over the pinned kernel tree; the CI job runs this target. |
| `make check-PAL` | `libcrug/pal` and the contract header | The contract case count, zero failed, with the fake PAL skipping one host-only case. |
| `make check-DIAGNOSTICS` | `crug.dg/diagnostics` | The golden comparison (50 match, 0 drifted) and the SARIF arm. |
| `make check-DOCS`, `check-docs-examples` | `docs/crug/` | Manifest conformance and example stdout comparison. |
| `make check-FUZZ` | `fuzz/` | `fuzz: 6 harness(es), 0 crash(es), 0 timeout(s), 0 new finding(s)` then `fuzz: OK`. |
| `make check-SANITIZER` | `libcrug/core`, `libcrug/tests/runtime` | ASan/UBSan reports fail; TSan is clean on the OS-thread channel arm; the fiber exception is ADR 0028. |
| `make check-PERFORMANCE` | `bench/` | Qualification modes pass, `performance: OK`, and `bench/results/<date>.md` is written. |

`make check-FUZZ FUZZ_BUDGET=2000` is the nightly budget; the default is 300 inputs per compiler target, and `FUZZ_RUNS` drives the runtime harnesses. The sanitizer gate exits 2, not 1, when gcc, clang or a sanitizer runtime is missing, so "could not run" is never "ran clean".

## 3. Golden corpora

A golden corpus records an observable that must not drift silently. There are six, and each has exactly one writer.

The diagnostic corpus is `gcc/testsuite/crug.dg/diagnostics`: `top50.txt` has exactly 50 rows of the form `<code> | cases/<name>.c | <flags> | <note>`, and `golden/<code>.txt` holds the exact diagnostic stream. The checker compiles each case with `-fsyntax-only -fno-diagnostics-color`, requires the `[crug-AREA-NNN]` code and the note when the fourth field is not `-`, and compares byte for byte; drift names `scripts/check-diagnostic-goldens.sh --update`, the corpus's only writer. `scripts/check-diagnostics.sh` guards the registry: every code referenced under `docs/` and `spec/` needs a row in `docs/crug/DIAGNOSTICS.md`, in a known area, no duplicate. The SARIF arm requires the code as `ruleId`.

The object and codegen corpus is `corpus/`: 30 plain-C programs compiled twice and compared, for source behavior and object identity, zero differing files in both arms. Alias facts add `crug.dg/alias` and the benchmark's byte-identity arm, since an alias fact may change an object only towards `restrict`.

The migration corpus is `migration/`: eight programs, `migration/expected/*.out`, and `migration/libc.crugffi` for the `fopen`/`fclose` case. Its test builds the original with `cc` and the migrated source with `crugc`, requiring both runs to agree.

The documentation corpus is six marked examples in `docs/crug/MIGRATION-EXAMPLES.md`, each marked with `<!-- example: NAME flags: ... -->`, a fenced C block, `<!-- expect -->` and the exact stdout; `scripts/check-docs-examples.sh` extracts, compiles, runs and compares. Nothing in the page is trusted, including the flags.

The kernel corpus is the pinned object list plus `kernel/object-diff-allow.txt`. Every allowed difference is one row with its explanation; the gate accepts 436 identical objects and exactly two explained differences (the build timestamp object and the compiler identification object), so a new difference is either a bug or a reviewed allowlist row, never a count edit.

The fuzz corpus is 45 committed seeds under `fuzz/compiler/corpus`: parser 12, ownership 10, borrow 11, ffi 12, adapted from the DejaGnu fixtures. Findings and reproducers go under `build/fuzz/findings` and are never committed until triaged; a real finding becomes a ledger entry with a severity before the fix lands, per `docs/crug/RELEASE-POLICY.md` section 3.1.

Never hand-edit a golden, review the diff `--update` produced, give every allowlist row a reason, and keep the compiler version next to the record.

## 4. The gate adjudicator

`scripts/gate.sh` is the only tool that decides a phase gate. It reads the CHECK lines of `docs/crug/EXIT-GATES.md`, not a second list, and writes its evidence into the repository.

```sh
scripts/gate.sh --list                    # phase N: how many CHECK lines
scripts/gate.sh --dry-run phase-22        # print the commands, run nothing
scripts/gate.sh --run phase-22 --timeout 600
scripts/gate.sh --run all                 # every phase, in file order
```

It finds the root with `git rev-parse --show-toplevel` and prepends `gcc/` to `PATH`, which is what makes the bare name `crugc` in a CHECK line mean `gcc/crugc`. Each selected CHECK runs from the root with `bash -c "$cmd" </dev/null` under `timeout` (default 120 seconds), so a check cannot read the gate document it came from or be fed the following lines through a booting guest's stdin. stdout and stderr are captured together.

The verdict rules are narrow on purpose. Exit 124 is TIMEOUT. An expected field naming `exit N` passes exactly when the exit status is N. An expected field saying empty or no output passes when the status is 0 and the output is blank. Anything else is MANUAL: a printed marker such as `fuzz: 6 harness(es), 0 crash(es), ...` is confirmed by reading the archived log, because the adjudicator does not pattern-match prose. Every check is archived as `docs/crug/gates/phase-N/<short-sha>/NNN.log` with a header repeating phase, ordinal, exit status, command and expected observable, then `---` and the output.

The loop runs every selected check and prints `checks: N, failed: N, timed out: N` plus the log directory, exiting 1 if anything failed or timed out and 0 otherwise. This differs from `docs/crug/EXIT-GATES.md` section 2, which says `scripts/gate.sh phase-N` stops at the first mismatch: the implemented invocation is `--run phase-N`, and the loop continues after a failure so one run collects every failure. The archive, not the loop, is the record.

The declaration rule still governs. A gate is adjudicated on a clean checkout of a recorded commit; an uncommitted tree adjudicates nothing. The archive is committed with the code. A missing script or corpus makes the CHECK fail by construction (127, command not found), so a gate cannot pass by omission. A `VAGUE:` statement is open until the owner automates it or an ADR records the manual procedure and its artefact, and roadmap section 35.3 stop conditions override every green line.

## 5. Continuous integration

Four workflows under `.github/workflows/` corroborate the local gates; none replaces an archive in `docs/crug/gates/`.

`kernel-compat.yml` runs on push and pull request to `main` and on dispatch. Its single `ubuntu-24.04` job has a 300-minute timeout, checks out one commit, frees the runner disk, requires 12 GiB free, installs `build-essential flex bison libgmp-dev libmpfr-dev libmpc-dev zstd`, runs `make baseline JOBS=$(nproc)` and then `make check-KERNEL_COMPAT`. `scripts/check-ci-gate.sh` reads the run and branch protection through the API and prints `ci-gate: kernel-compat success=<sha> required=true` only when the check passed and is required, so "CI is green" cannot be claimed from an optional job.

`multi-os.yml` runs `make check-MULTI_OS` on four hosts: `ubuntu-24.04` (which also cross-builds aarch64 and runs it under qemu), `ubuntu-24.04-arm`, `windows-2022` and `macos-14`. Each job's last log line names its platform and counts, so a red host and a red local `scripts/check-multi-os-ports.sh` mean the same thing.

`fuzz-nightly.yml` is the evidence source for the nightly gate. It runs on `schedule` with the quoted cron `'17 3 * * *'`, on `workflow_dispatch`, and on push to `main` touching `fuzz/**`, `scripts/check-fuzz.sh`, `scripts/check-sanitizer.sh` or its own file. One `ubuntu-24.04` job with a 120-minute timeout frees disk, requires headroom, installs the build packages plus `clang` and its matching `libclang-rt-<major>-dev`, relaxes mmap entropy for TSan, builds the baseline, runs `make check-FUZZ FUZZ_BUDGET=2000` and then `make check-SANITIZER`. Its header comment still describes an older debug-publish block that is not in the steps.

`scripts/check-fuzz-nightly.sh --window 30` counts the consecutive scheduled successes immediately before the newest scheduled run, reading the public API first and the workflow HTML `embeddedData` second. Today it exits 2 with `check-fuzz-nightly: api: the workflow has no scheduled runs yet`: the workflow is in the tree but has no schedule history, so the nightly gate is open by construction until 30 consecutive scheduled successes exist. A push run does not count; a newer failed or in-progress scheduled run resets the window; unavailable data is exit 2, never a pass.

`gcc-drift.yml` is the monthly drift check (cron `0 6 1 * *`, dispatch): it checks out the vendor tag with full history and runs `scripts/fork-delta.sh` and `scripts/check-gcc-drift.sh`. It never builds and never edits the repository.

CI limits matter. The runners are ephemeral and their logs are not the archive the gate requires. `kernel-compat` is Linux x86-64 only; the multi-OS jobs prove the runtime ports, not a kernel build. A green pull-request job is not a gate pass until the merge commit's archive exists.

## 6. Adding a test

Pick the layer by the observable, then add the fixture where the loader looks.

For a compiler rule, add a `.c` file to `gcc/testsuite/crug.dg/<dir>` with its own flags. A compile-only reject case is:

```c
/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */
/* { dg-error "use after move" "\[crug-UAM-001\]" { target *-*-* } 0 } */
```

Put the code after the directives, use `dg-final` for dump probes, keep the fixture self-contained; `crug.exp` sets an empty `DEFAULT_CFLAGS` because `-ansi -pedantic-errors` does not describe the dialect. Run `make check-<SUITE>` and read `build/obj/gcc/testsuite/gcc/gcc.sum`; expected-pass totals are not hand-maintained, so do not edit a count.

For an observable a dg directive cannot express, add or extend a `scripts/check-*.sh`: cd to the root via `dirname`, print the tally line the gate will quote, exit 2 on a setup error, 1 on a failed check, keep scratch under `build/`, and make it deterministic. Register a `Makefile` target only when the phase owner wants it gated.

For runtime behavior, add a plain-C program under `libcrug/tests/<area>`, link it as its neighbours do, assert exact counters, bound its runtime, and call it from the area's suite script. The 100,000-task and scope stress scripts are the models. For the kernel corpus, prefer a small translation unit: add it to the pinned list, run `scripts/kernel-stage.sh K4` and `scripts/kernel-object-diff.sh`, and if the object legitimately differs, add one row with its explanation to `kernel/object-diff-allow.txt`, expecting the gate line to name one explained difference, not zero.

For a fuzz seed, copy a small self-contained fixture into the target directory, keep it under 8 KiB, and note that the file name is part of the deterministic stream. Update the counts in `fuzz/compiler/README.md` and run a small budget first.

For a diagnostic, follow `docs/crug/DIAGNOSTICS.md` section 7: record the origin decision, add the registry row with default and strict severity, add `gcc/testsuite/crug.dg/diagnostics/<code>.c` whose `dg-error` names the code, and add the option row if it is a warning. Then run `scripts/check-diagnostic-goldens.sh --update`, review the new `golden/<code>.txt`, and re-run the check plus `scripts/check-diagnostics.sh`. Add a `top50.txt` row only when the class belongs to the published fifty.

For documentation, run `make check-docs-examples` after editing `MIGRATION-EXAMPLES.md` and `make check-DOCS` after any page change; a sentence offering a post-1.0 feature must say so in the same paragraph.

The final checklist for a compiler change is: the owning suite, `scripts/check-diagnostics.sh` if a code was added or reworded, `make check-docs-examples` if an example changed, `scripts/gate.sh --dry-run` for the phase, then `scripts/gate.sh --run phase-N` and the archive. A new defect is a `docs/crug/KNOWN-ISSUES.md` entry with a severity before the fix lands, and a fixed P0/P1 entry names an existing regression path that `scripts/check-regressions.sh` can execute.

## 7. Coverage archive

Coverage is recorded as provenance, not as a threshold. `scripts/run-coverage.sh`
builds the compiler with GCOV instrumentation, runs the crug.dg suites and writes
`docs/crug/coverage/<HEAD>.json`: the commit, the `cc1` hash, the list of
instrumented files, the per-file line counts and the computed totals.
`scripts/check-coverage.sh` validates the schema, that the recorded HEAD is the
current HEAD, the hashes and the internal arithmetic, and compares the totals
with the ledger block in this file; a missing archive exits 2, and there is no
minimum percentage, because the number is only meaningful next to the commit it
was measured at. The archive is data, not a manifest document, so the
45-document set in `docs/crug/REQUIRED-DOC-SECTIONS.md` is unchanged.

The post-rc1 fix commit did **not** regenerate an archive: the GCOV build
artifacts are not in the repository, and the archive must be produced from a
build of the exact commit it names. The last measurement recorded before the
fixes is 2,267 of 4,196 instrumented lines in 12 files (`cc1`
`d8261e97...`, recipe and per-file table in `spec/fix-p1/COVERAGE.md`);
that measurement describes the pre-fix tree and must not be quoted for the fix
commit. Until the owner regenerates it, the weekly CI coverage job fails with
exit 2 (data unavailable), which is the documented behaviour for an absent
archive rather than a passing result.
