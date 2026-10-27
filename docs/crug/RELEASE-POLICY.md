# Release Policy

**Status:** Ratified for M21 and M22 on 2026-10-20. This file owns the numbers
every gate cites; a threshold that is not written here cannot adjudicate a gate.

The policy has three jobs. It fixes the **performance qualification thresholds**
(Section 2) that Phase 21 and the Phase 25 release candidate enforce, the
**security and reliability budgets** (Section 3) that Phase 22 enforces, and
the **release-candidate quality thresholds** (Section 4) that Phase 25 and 1.0
are adjudicated against. Versioning and compatibility promises are not
duplicated here: they belong to `docs/crug/COMPATIBILITY-POLICY.md` (Phase 23).

## 1. How a threshold is ratified

- Every gate line that carries a number names a section of this file.
- A number changes only by an amendment row in Section 5, and the gate that
  cites it is re-run and archived at the new commit.
- The evidence behind each number is measured, not asserted: it lives in
  `docs/crug/PERFORMANCE.md` for Section 2, in the fuzz and sanitizer logs under
  `build/` for Section 3, and in `docs/crug/KNOWN-ISSUES.md` for Section 4.
- A threshold guards against a regression; it is not a target to optimise
  against. "Measured faster than the band requires" is not a release claim.

## 2. Performance qualification (M21)

### 2.1 Zero cost - no tolerance

`bench/run.py --zero-cost --check` compares four Crug constructs with
their plain-C twins at `-O0`, `-O2` and `-O3`:

| case | Crug construct | twin |
|---|---|---|
| `own-drop` | owned allocation, automatic drop | guarded `free` |
| `move` | `move (p)` of an owned parameter | plain assignment |
| `ref-param` | `ref const int *` loop | `const int *` loop |
| `mut-param` | `mut int *` loop | `int *` loop |

The threshold is exact: **0 additional instructions, 0 additional calls, and
a byte-identical optimised tree dump** on all twelve case/level pairs. Local
label numbers and temporary numbers are canonicalised before the comparison;
they carry no meaning. No tolerance, because a "small" overhead per construct
is how a zero-cost claim decays.

### 2.2 Plain-C codegen parity

`bench/run.py --pristine --check` compiles every `corpus/*.c` program
and every `bench/kernels/*.c` translation unit with the unmodified GCC 16.2
compiler (built by `make pristine-xgcc`) and with `gcc/crugc` at
`-O0`, `-O2` and `-O3`.

- **Threshold:** every object is byte-identical (114 source/option pairs).
- **LTO:** the final executables at `-O2 -flto` are compared by digest;
  when they differ the 1.02 timing band applies. LTO objects are not
  byte-reproducible, so object identity cannot be required.

### 2.3 Recorded plain-C baseline drift

`bench/run.py --plain-c --check` compares against
`bench/baseline.json`:

- **Objects:** byte-identical at `-O0`/`-O2`/`-O3`. This is the
  enforcement.
- **Timing:** the 1.02 band applies only when the object changed, because a
  changed object is a real codegen change. Identical objects are reported as
  1.000 regardless of the measured ratio.
- **LTO arm:** builds, runs and agrees on the result; timing is informational.

Rationale: this host's run-to-run swing on one executable exceeds a factor of
two, so a timing-only gate would flap while proving nothing. Object identity is
deterministic and is the evidence; the band is a secondary guard.

### 2.4 Task dispatch and memory

`bench/concurrency/run.py --check` spawns 200 tiny tasks in three arms
(pthread-per-task, an allocation-free C thread pool, and the Crug runtime) and
reads the runtime counters. Thresholds:

- **Correctness:** 200 of 200 tasks run exactly once; the arms agree on the
  checksum; `lost = 0`.
- **Dispatch:** Crug dispatch time <= **1.5 x** pthread-per-task.
- **Memory:** Crug reserved bytes per live task <= **0.5 x** pthread-per-task
  (VmSize delta / number of live tasks).
- **Against the allocation-free pool:** recorded, not gated. The pool reuses a
  preallocated queue and needs no per-task resource; Crug reserves and commits
  a 64 KiB guarded stack per task. The measured ratio therefore stays far
  above 1.5, and the gap is the documented cost of per-task stack ownership.
  A bounded allocation cache (ADR 0008 A-2) now removes the per-spawn stack
  reserve/commit and task malloc from the hot path; the thresholds above are
  unchanged.

### 2.5 Channels

`bench/concurrency/run.py --check` also measures a ping-pong pair, a
200,000-element stream and the 1/2/4-pair scaling of the same stream.

- **Correctness:** every element arrives once, in order, checksum equal.
- **Latency:** < **10 microseconds** per round trip.
- **Throughput:** > **1,000,000 elements/s** at capacity 1024 on this host.
- **Memory:** < **64 KiB** per channel.
- **Scaling:** the 1/2/4-pair numbers are recorded and reviewed, not gated;
  this host is shared and oversubscribed.

### 2.6 Kernel codegen

`scripts/check-kernel-perf.sh` compares the 438-object corpus of
`kernel/KERNEL-CORPUS.md` built by `gcc/crugc` with the same corpus
built by the wrapped GCC driver.

- **Threshold:** 436 of 438 objects byte-identical. The two explained objects
  are `init/version-timestamp.o`, whose `UTS_VERSION` embeds build time and
  build number, and `init/version.o`, whose compiler identification comes from
  `$CC --version` (`crugc 0.1.0` for the launcher, `xgcc (GCC) 16.2.0` for the
  reference driver); both reasons must be recorded in
  `kernel/object-diff-allow.txt`, and `CONFIG_CC_VERSION_TEXT` is filtered from
  the configuration comparison because it carries the same identification.
- **Also identical:** `.text` size, symbol counts and the disassembly hash
  for all 438 objects.
- **Sizes recorded:** vmlinux and bzImage byte sizes.

### 2.7 Method and environment

- Reference host for the ratified numbers: Linux x86-64, 20 logical CPUs.
  Absolute numbers are host-specific; the *bands* are what a gate enforces.
- Timings take the minimum of repeated runs: the harness reports the best of
  7 internal repetitions, and the driver takes the best of 3 processes for the
  recorded baseline and of 8 for the concurrency arms.
- Results are written to `bench/results/<date>.md` (gitignored); the
  checked-in narrative is `docs/crug/PERFORMANCE.md`.
- `make check-PERFORMANCE` runs all five checks and writes the report.

## 3. Security and reliability qualification (M22)

### 3.1 Fuzz harnesses: zero tolerance

`make check-FUZZ` runs six harnesses: four compiler targets under
`fuzz/compiler` (the contextual parser, ownership state transitions, the borrow
checker, the `.crugffi` parser) and two runtime harnesses under `fuzz/runtime`
(the scheduler queue and the channel state machine).

- **Default budget:** 300 mutated compiler inputs per target (`FUZZ_BUDGET`),
  and the runtime defaults declared by `fuzz/runtime/run.sh` (2000 scheduler
  sequences and 20000 channel runs, `FUZZ_RUNS`). The nightly workflow raises
  the compiler budget to 2000.
- **Threshold:** 0 crashes, 0 timeouts, 0 internal compiler errors and 0 new
  findings. A finding is any artifact written under the findings directory; it
  must be triaged into `docs/crug/KNOWN-ISSUES.md` with a severity from
  roadmap section 3 before the gate can be green again.
- **Corpora:** seeds are committed under `fuzz/compiler/corpus/`; artifacts are
  build output. A crash artifact is never committed as a seed until it is
  triaged and fixed.
- **Determinism:** the compiler harness re-runs to the same inputs from a fixed
  `--seed`; a flaky harness is a defect in the harness, not evidence.

### 3.2 Sanitizers

`make check-SANITIZER` runs the task, scope and channel suites plus the stress
programs under AddressSanitizer and UndefinedBehaviorSanitizer with
abort-on-error and leak detection, and a ThreadSanitizer arm over an OS-thread
channel exchange.

- **Threshold:** no ASan, UBSan, LSan or TSan report. Suppressing a report
  through a sanitizer option is not allowed; narrowing the arm is.
- **TSan and fibers:** the v1 context switch is not annotated with TSan's fiber
  API, so TSan is applied only where no stack switch occurs. The exception is
  recorded in `docs/crug/adr/0028-tsan-fiber-exception.md`; a future
  fiber-hook increment must remove the exception, not widen it.

### 3.3 Nightly window

`scripts/check-fuzz-nightly.sh --window 30` reads the runs of
`.github/workflows/fuzz-nightly.yml` (event `schedule`) and requires 30
consecutive successful scheduled runs.

- The window is the count of consecutive `success` scheduled runs immediately
  before the newest scheduled run; a failed or in-progress scheduled run resets
  it.
- A push-triggered run is useful evidence but does not count toward the window.
- Data unavailability (rate limit, no runs yet) is exit 2 from the checker and
  is not evidence of success.
- The period is the weakest defensible window; roadmap section 26 leaves it to
  release management, so 30 days is the ratified choice.

### 3.4 Defect ledger

`scripts/check-p0-open.sh` and `scripts/check-regressions.sh` enforce:

- **0 open P0** entries in every scope (roadmap section 3 severities).
- **Every fixed P0/P1 entry names an existing regression path**; the checker
  fails when the path is missing, and `--run` executes the path when it is
  executable. A fix without a regression test is not fixed.
- A new defect found by fuzzing, a sanitizer run or a stress run is added to
  the ledger before its fix lands; the severity decides what blocks the gate.

### 3.5 Method

- Reference host: Linux x86-64, 20 logical CPUs; compiler `gcc/crugc`;
  runtime built from `libcrug/`.
- Logs: `build/fuzz/compiler.log`, `build/fuzz/runtime.log`,
  `build/libcrug-sanitizer/`; findings under `build/fuzz/findings/`.
- Commands: `make check-FUZZ`, `make check-SANITIZER`; the nightly workflow runs
  the same two commands with the larger budget.

## 4. Release-candidate thresholds (M23)

The Phase 25 gate takes these numbers from this section:

- **Open P0 findings: 0.** No exception; a P0 blocks the RC and 1.0.
- **Open P1 findings: at most 2** at the RC tag, each with an owner-signed
  waiver row in `docs/crug/KNOWN-ISSUES.md` naming the affected scope and
  the reason it is acceptable; for **1.0 the number is 0**. No waiver
  mechanism exists for 1.0.
- **Fuzz window:** the Phase 22 harnesses run green for 30 consecutive scheduled
  days (Section 3.3).
- **Kernel:** full primary build and boot smoke green at the RC commit
  (Phase 19 stages K5 and K6).
- **Ports and performance:** `make check-MULTI_OS`, `make check-FFI` and
  `make check-PERFORMANCE` all exit 0 at the RC commit.

`scripts/count-open-p1.sh --max=2` is the Phase 25 command; the script takes
the number from this section and fails when an open P1 has no waiver row. The
waivers live in the `## 4. P1 waivers` table of `docs/crug/KNOWN-ISSUES.md`:
each row names the entry, the signing owner, the decision date, the scope and
the reason. A row without a signature, a row that names no open P1, or an open
P1 with no row at all fails the check. For 1.0 the command runs with
`--max=0`, so any waiver row makes the check fail.

## 5. Compatibility promises

Versioning (language, Crug ABI, `.crugffi`, libcrug ABI), deprecation and
the supported platforms are defined by `docs/crug/COMPATIBILITY-POLICY.md`
(Phase 23). This policy does not restate them; it only fixes the numbers that
the qualification gates enforce.

## 6. Amendments

| amendment | date | change | why |
|---|---|---|---|
| A-1 | 2026-10-20 | Initial ratification for M21 and the M23 quality thresholds. The dispatch gate compares against pthread-per-task, not against the allocation-free pool; the pool ratio is recorded. | An allocation-free pool is not the C baseline a project starts from, and a 64 KiB stack per task is a design decision of the runtime (ADR 0008), not an overhead of the language. The zero-cost claims are gated exactly; the pool ratio is evidence for future stack-pooling work. |
| A-2 | 2026-10-20 | Ratified the M22 fuzz budgets (300 compiler inputs per target, 2000 sequence/20000 channel-run runtime defaults, 2000 inputs nightly), the sanitizer scope and the 30-consecutive-scheduled-run nightly window. | Phase 22 is the first phase whose evidence is statistical rather than a fixed command output: a budget makes the run reproducible, and a window makes "continuously running" falsifiable. The window length is release management's call (roadmap section 26) and 30 days is the weakest defensible choice. |
| A-3 | 2026-10-21 | Phase 25 mechanics: the P1 threshold is 2, the waiver rows live in `docs/crug/KNOWN-ISSUES.md` section 4 and are read by `scripts/count-open-p1.sh`, and the release-candidate gate runs the archive, ledger, nightly-window, kernel, port/performance and freeze checks. | Section 4 fixed the numbers but not where a waiver is recorded or who signs it, so the gate could not distinguish a waived P1 from an unwaived one; the phase-25 checks also needed the RC tag and the scheduled-run window named as evidence rather than implied. |
| A-4 | 2026-10-22 | The kernel byte-identity threshold is 436 of 438 objects with two explained objects, not 437 with one. | Phase 24 made `crugc --version` report the launcher's identity and the kernel embeds that string in `init/version.o`, so the object joined `init/version-timestamp.o` as an explained difference; the old count could no longer be printed. The threshold is a claim about codegen, and no compiler or runtime object changed, so the substance of 2.6 is unchanged. |
| A-5 | 2026-10-22 | Phase 24's release surface is five artefacts: the tarball, the SPDX SBOM, the Debian package, `LICENSE` and `NOTICE`. The package is first-class evidence, so the checksum set, `scripts/check-release.sh`, the apt install-and-remove smoke and `make check-DEB` / `make check-launcher` verify it alongside the tarball. No M21, M22 or M23 threshold changes. | Section 2 measured the compiler, not how it is delivered, so a package could have been added without any threshold noticing. Naming the deb in the surface makes `dist/SHA256SUMS` and the Phase 24 gate the place where a delivery defect is caught, while the performance and quality numbers stay exactly as ratified. The package is `amd64` only, unsigned and in no repository: those are recorded as open work in `docs/crug/progress.md`, not as a weaker threshold. |
