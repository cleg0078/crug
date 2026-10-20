# Fuzzing, Sanitizers and the Reliability Window

**Status:** Delivered in Phase 22 (2026-10-20). The budgets and thresholds in
this document are ratified in [RELEASE-POLICY.md](RELEASE-POLICY.md) section 3;
this document is the operator's guide. The executable gate is
`make check-FUZZ`, `make check-SANITIZER`, the two ledger checkers and
`scripts/check-fuzz-nightly.sh`; see [EXIT-GATES.md](EXIT-GATES.md) phase 22.

## 1. What is fuzzed, and what is not

The qualification surface is fixed by MASTER_SPEC section 50-51 and roadmap
M22: the compiler front end must not crash on malformed Crug source, the
runtime must not lose, duplicate or corrupt work under randomized operation
sequences, and the defect ledger must show no known P0 defect and a regression
test for every fixed P0/P1.

Six harnesses cover that surface:

| Harness | Flags | Stresses | Budget |
| --- | --- | --- | --- |
| `fuzz/compiler` target `parser` | `-fcrug-ownership` | contextual `own`/`ref`/`mut`/`move` tokens, declarators, error recovery | 300 inputs |
| `fuzz/compiler` target `ownership` | `-fcrug-ownership` | move, use-after-move, implicit copy, `adopt`/`release`/`drop`, branch joins | 300 inputs |
| `fuzz/compiler` target `borrow` | `-fcrug-borrow` | shared/mutable borrow rules, reborrows, last use | 300 inputs |
| `fuzz/compiler` target `ffi` | `-fcrug-ffi=<sidecar>` | `.crugffi` parser and contract checker | 300 inputs |
| `fuzz/runtime` scheduler | runtime API | task/scope/channel operation sequences, exactly-once completion, counters, idempotent shutdown | 2000 sequences |
| `fuzz/runtime` channel | runtime API | channel state machine: FIFO, readiness, close, stats, `len` | 20000 runs |

Not claimed:

- **No formal verification and no soundness proof.** A clean fuzz run is
  evidence about the inputs that were tried, not about all inputs.
- **No memory-safety claim inside `unsafe`.** That is the documented contract
  of the block (ADR 0021); fuzzing does not change it.
- **No path coverage figure.** The compiler harness is mutational and
  seed-driven; coverage is not measured or gated.
- **No TSan coverage of the fiber switch.** See section 5.

## 2. The compiler harness (`fuzz/compiler`)

`fuzz/compiler/fuzz.py` is a deterministic, standard-library-only mutational
fuzzer. It reads committed seeds from `fuzz/compiler/corpus/<target>/` in
sorted order, draws a random splice partner, applies 1-4 mutation operators
(byte splice, line duplication or deletion, token replacement or swap from the
C/Crug dictionary, truncation, dictionary insertion), and compiles each mutant
with

```
gcc/crugc -fsyntax-only -fno-diagnostics-color <target flags> <mutant>
```

with a 10-second timeout. Classification:

- **crash** - the process died from a signal (negative return code) or stderr
  contains `internal compiler error` or `Segmentation fault`;
- **timeout** - the run exceeded 10 seconds and its process group was killed;
- **ok** - anything else, including an ordinary Crug rejection. Rejections are
  expected; a nonzero exit status alone is not a finding.

A crash or timeout is written once per unique normalized signature, so one bug
does not produce hundreds of files. A finding is a directory per target:

```
build/fuzz/findings/<target>/<hash>.c          exact input (plus .h/.crugffi for ffi)
build/fuzz/findings/<target>/<hash>.log        kind, signature, reproduce command, stderr excerpt
build/fuzz/findings/<target>/crashes.log       one appended line per saved finding
```

Determinism: the PRNG is seeded from `("<seed>:<target>")`, the corpus is read
in sorted order, and signatures redact temporary paths, absolute paths and hex
addresses. `--seed 20261020` reproduces the gate's stream exactly;
`FUZZ_BUDGET=2000` is what the nightly job uses. The findings directory is not
cleaned between runs: delete `build/fuzz/findings` before a clean gate run, and
never commit a crash artifact as a seed before it is triaged and fixed.

## 3. The runtime harnesses (`fuzz/runtime`)

Both harnesses are built by `fuzz/runtime/run.sh` into
`build/fuzz/runtime/` and can run from any directory.

**Scheduler harness** (`scheduler_fuzz.c`, gcc with ASan and UBSan). Each
sequence is a random operation list against `task.h` and `chan.h` (spawn,
yield, nested scopes, channel create/send/recv/close, shutdown). The oracle is
built by the main thread while the operations are issued, never by the tasks:
every spawned task completed exactly once, per-slot counters agree, nothing was
lost or corrupted, no scope is left live, stats deltas match, and a second
shutdown is a no-op. A failure prints the sequence number and the seed so the
sequence can be re-run alone with `FUZZ_SEED`.

**Channel harness** (`channel_fuzz.c`, clang libFuzzer). The input bytes select
1-4 channels (capacity 1-8) and up to 64 operations with readiness checks. A
ring-buffer model asserts FIFO order, readiness, closedness, `len`/`cap` and
the stats counters after every operation, so a broken state machine is a
`FUZZ-INVARIANT` abort rather than a silent pass.

A negative control was run once to prove the channel harness can fail: a
build-only copy of `libcrug/core/chan.c` with `len = chan->count + 1u` made
the harness exit 77 with `FUZZ-INVARIANT: op 3: len disagrees with the model`.
The broken copy was deleted; only the log under `build/` remains.

## 4. Running the checks

```sh
make check-FUZZ                        # six harnesses, default budgets
FUZZ_BUDGET=2000 make check-FUZZ       # the nightly budget
make check-SANITIZER                   # ASan/UBSan/LSan plus the TSan arm
scripts/check-p0-open.sh               # 0 open P0 defects
scripts/check-regressions.sh           # every fixed P0/P1 has a regression path
```

`make check-FUZZ` runs both harness groups, tees their output to
`build/fuzz/compiler.log` and `build/fuzz/runtime.log`, and counts new files
under `build/fuzz/findings`. Its acceptance lines are
`fuzz: 6 harness(es), 0 crash(es), 0 timeout(s), 0 new finding(s)` and
`fuzz: OK`; it exits 2 when a harness or its toolchain is missing, which is not
a pass.

## 5. Sanitizers

`make check-SANITIZER` (scripts/check-sanitizer.sh) has three arms:

1. `scripts/check-tasks-sanitizer.sh` - the task, scope and stress suites
   built with ASan and UBSan, abort-on-error, leak detection on;
2. ASan and UBSan over the channel programs and an OS-thread channel/PAL
   stress program;
3. clang ThreadSanitizer over the same OS-thread stress program, built with
   `-DCRUG_TSAN_ARM`.

The TSan arm is deliberately limited to code that does not switch stacks. The
v1 context switch is not annotated with TSan's fiber API
(`__tsan_create_fiber`/`__tsan_switch_to_fiber`), so TSan cannot see task
races, task-to-task happens-before across a switch, the scheduler queue or
scope state; those paths are covered by ASan+UBSan only. That exception, its
exit criteria and the alternatives are recorded in
[adr/0028-tsan-fiber-exception.md](adr/0028-tsan-fiber-exception.md).
Suppressing a report through a sanitizer option is not allowed; narrowing an
arm is, and the exception is an ADR.

One v1 runtime limitation is visible here: `libcrug/core/chan.c` keeps one
`malloc`ed receive buffer per thread with no release entry point, so a
short-lived receiving OS thread would trip LeakSanitizer. The stress program
therefore receives on the main thread and uses four producer threads; the gap
is recorded in ADR 0028.

## 6. The nightly job and the reliability window

`.github/workflows/fuzz-nightly.yml` runs on a schedule (`17 3 * * *` UTC), on
`workflow_dispatch`, and on pushes that touch `fuzz/**`,
`scripts/check-fuzz.sh` or `scripts/check-sanitizer.sh`. Its single
`ubuntu-24.04` job mirrors the kernel-compat workflow (checkout, free disk
space, build dependencies), runs `make baseline JOBS=$(nproc)`, then
`make check-FUZZ FUZZ_BUDGET=2000` and `make check-SANITIZER`.

The job installs `libclang-rt-<major>-dev` next to `clang`: Ubuntu's `clang`
package does not pull compiler-rt, so without it `-fsanitize=thread` and
`-fsanitize=fuzzer` fail to link (the scripts exit 2, a missing tool, not a
report). It also lowers `vm.mmap_rnd_bits` to 28, because clang 18's TSan cannot
map its shadow with the 32-bit mmap entropy of an ubuntu-24.04 runner.

`scripts/check-fuzz-nightly.sh --window 30` counts the consecutive successful
scheduled runs immediately before the newest scheduled run and prints
`fuzz-nightly: N consecutive scheduled success(es), window 30, earliest ...,
latest ...`. Exit status:

- **0** - the window is met (N >= 30);
- **1** - the window is not met; a failed or in-progress scheduled run resets
  the count;
- **2** - the data is unavailable (no runs yet, rate limit, HTTP failure). Exit
  2 is not evidence of success and never turns the gate green.

Push-triggered runs are useful evidence but do not count toward the window: the
point of the window is the calendar, not a burst of pushes. The 30-day window
is the ratified choice in RELEASE-POLICY 3.3; shortening it is an amendment to
that policy, not a gate edit.

## 7. Triage: from a finding to a regression test

Every finding follows one path: reproduce, classify, ledger, fix, regression,
re-run. A finding is not "triaged" by deleting it.

1. **Reproduce.** The `.log` file contains a copy-pasteable command. For the
   scheduler harness, `FUZZ_SEED=<seed>` re-runs the sequence alone.
2. **Classify.** Assign a severity from roadmap section 3 (P0/P1/P2/P3) and add
   a row to the machine-readable ledger in [KNOWN-ISSUES.md](KNOWN-ISSUES.md)
   **before** the fix lands: `id severity scope status regression summary`.
3. **Fix** the compiler or runtime (for a compiler ICE, the crash is a compiler
   bug even when the input is invalid C).
4. **Add a regression test** and name its path in the ledger's `regression`
   field. `scripts/check-regressions.sh` fails when the path does not exist;
   a fix without a regression test is not a fix.
5. **Re-run** `make check-FUZZ`; the gate is green only when no new finding
   appears and the ledger has no open P0.

### The worked example: K-002

The Phase 22 compiler harness found 46 crashes with 35 unique signatures; all
of them were one bug. A parse error can leave `error_mark_node` as the function
body, and `crug_check_function` then called `BIND_EXPR_BODY` on it:

```
int f (void) * x { return 0; }     /* -fcrug-ownership */
tree check: expected bind_expr, have error_mark in crug_check_function
```

The fix guards the prologue against `NULL_TREE` and `error_mark_node`, and
the regression test is
`gcc/testsuite/crug.dg/ownership/error-mark-body.c` (a `dg-error` plus a
`dg-prune-output` for the EOF diagnostic). K-002 is a P1 in the ledger with
that fixture as its regression path; the fuzz harness must stay at 0 crashes.

A finding is never "fixed" by accepting the crash as expected behaviour.

## 8. Measured evidence (2026-10-20)

| Check | Result |
| --- | --- |
| `make check-FUZZ` | 6 harnesses, 1200 compiler inputs, 2000 scheduler sequences, 20000 channel runs, 0 crashes, 0 timeouts, 0 new findings |
| `make check-SANITIZER` | ASan/UBSan/LSan clean over tasks, scopes and channels; TSan clean on the OS-thread arm; fiber exception in ADR 0028 |
| `scripts/check-p0-open.sh` | 0 open P0 |
| `scripts/check-regressions.sh` | 2 fixed P0/P1 entries (K-001, K-002), 0 without a regression test |
| `scripts/check-fuzz-nightly.sh` | window 30 not met yet; the checker reports the truth and exit 2/1 until scheduled runs accumulate |

## 9. Limits of this qualification

- The compiler harness is mutational: it explores around committed seeds, not
  the whole grammar. A hand-written pathological input can still crash the
  compiler.
- The input cap is 8 KiB and the timeout is 10 s; a slow-but-correct compile is
  not a finding.
- Runtime exploration is randomized: the scheduler harness catches invariant
  violations over many sequences, not every interleaving. Formal verification
  is out of scope.
- Sanitizer coverage stops at the fiber switch (section 5) and at the channel
  receive-buffer lifetime gap.
- The window measures scheduled runs of one workflow on one OS; a platform
  specific runtime bug can hide until its port runs on CI (Phase 18).
- The reference host is Linux x86-64; the runtime fuzz harnesses are not part
  of `make check-MULTI_OS` for the other ports yet.
