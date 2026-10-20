# Release Policy

**Status:** Ratified for M21 on 2026-10-20. This file owns the numbers every
gate cites; a threshold that is not written here cannot adjudicate a gate.

The policy has two jobs. It fixes the **performance qualification thresholds**
(Section 2) that Phase 21 and the Phase 25 release candidate enforce, and it
fixes the **release-candidate quality thresholds** (Section 3) that Phase 25
and 1.0 are adjudicated against. Versioning and compatibility promises are not
duplicated here: they belong to `docs/crug/COMPATIBILITY-POLICY.md` (Phase 23).

## 1. How a threshold is ratified

- Every gate line that carries a number names a section of this file.
- A number changes only by an amendment row in Section 5, and the gate that
  cites it is re-run and archived at the new commit.
- The evidence behind each number is measured, not asserted: it lives in
  `docs/crug/PERFORMANCE.md` for Section 2 and in
  `docs/crug/KNOWN-ISSUES.md` for Section 3.
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
  Stack pooling is future work and needs its own ADR; it is not part of M21.

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

- **Threshold:** 437 of 438 objects byte-identical. The one explained object
  is `init/version-timestamp.o`, whose `UTS_VERSION` embeds build time and
  build number; its reason must be recorded in
  `kernel/object-diff-allow.txt`.
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

## 3. Release-candidate thresholds (M23)

The Phase 25 gate takes these numbers from this section:

- **Open P0 findings: 0.** No exception; a P0 blocks the RC and 1.0.
- **Open P1 findings: at most 2** at the RC tag, each with an owner-signed
  waiver row in `docs/crug/KNOWN-ISSUES.md` naming the affected scope and
  the reason it is acceptable; for **1.0 the number is 0**. No waiver
  mechanism exists for 1.0.
- **Fuzz window:** the Phase 22 harnesses run green for 30 consecutive days
  (ratified in the Phase 22 gate).
- **Kernel:** full primary build and boot smoke green at the RC commit
  (Phase 19 stages K5 and K6).
- **Ports and performance:** `make check-MULTI_OS`, `make check-FFI` and
  `make check-PERFORMANCE` all exit 0 at the RC commit.

`scripts/count-open-p1.sh --max=2` is the Phase 25 command; the script takes
the number from this section and fails when an open P1 has no waiver row.

## 4. Compatibility promises

Versioning (language, Crug ABI, `.crugffi`, libcrug ABI), deprecation and
the supported platforms are defined by `docs/crug/COMPATIBILITY-POLICY.md`
(Phase 23). This policy does not restate them; it only fixes the numbers that
the qualification gates enforce.

## 5. Amendments

| amendment | date | change | why |
|---|---|---|---|
| A-1 | 2026-10-20 | Initial ratification for M21 and the M23 quality thresholds. The dispatch gate compares against pthread-per-task, not against the allocation-free pool; the pool ratio is recorded. | An allocation-free pool is not the C baseline a project starts from, and a 64 KiB stack per task is a design decision of the runtime (ADR 0008), not an overhead of the language. The zero-cost claims are gated exactly; the pool ratio is evidence for future stack-pooling work. |
