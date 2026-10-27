# Changelog

All notable changes to Crug are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html): language, Crug ABI,
runtime ABI, FFI metadata and PAL versions are versioned separately, as
`gcc/crugc --version` reports.

## [Unreleased]

## [0.2.3] - 2026-10-27

### Changed

- **Spawned tasks materialize their stack when a worker dispatches them.**
  `crug_task_spawn` now copies the frame and queues a task whose stack is
  materialized by the worker that runs it (ADR 0008 A-3). A 480,000-goroutine
  burst that spawns before it drains cost 11.4 s and 32 GB of address space at
  eight workers; it now costs 0.65 s and 1.8 GB, and a queued task holds no
  stack. The spawn contract, the canary check and the 64 KiB default stack are
  unchanged; a task that never ran is intact by construction and releases its
  header without a canary check.
- **Tests and probes.** `libcrug/tests/runtime/burst_stacks.c` is the new
  Linux regression test (it fails with ~20 GiB of `VmPeak` on the eager
  implementation), and `make -C libcrug bench-tasks` now reports the spawn
  loop separately and the process `VmPeak`.
- **Docs and gate.** TASKS.md, PERFORMANCE.md section 4 and ADR 0008 record the
  change; the Phase 14 gate is re-baselined by amendment A-32 (compiler-test
  count 27 to 31, and the queue peak is printed but no longer adjudicated).
- **Release version.** `VERSION` and `crugc --version` now report `0.2.3`.
  The language version stays `0.1.0`, the Crug and runtime ABIs stay `1`,
  `.crugffi` stays `1.0` and the PAL contract stays `1.2`.

## [0.2.2] - 2026-10-27

### Changed

- **Task allocation caches.** `crug_task_spawn` now reuses task stacks through
  a bounded per-thread cache and task/frame blocks through size-class bins;
  corrupted slots are quarantined and all caches are released at shutdown.
  `CRUG_TASKS_CACHE` and `CRUG_TASKS_TASK_CACHE` control the caches.
- **Release version.** `VERSION` and `crugc --version` now report `0.2.2`.
  The language version stays `0.1.0`, the Crug and runtime ABIs stay `1`,
  `.crugffi` stays `1.0` and the PAL contract stays `1.2`.

## [0.2.1] - 2026-10-27

### Changed

- **Release version.** `VERSION` and `crugc --version` now report `0.2.1`.
  The language version stays `0.1.0`, the Crug and runtime ABIs stay `1`,
  `.crugffi` stays `1.0` and the PAL contract stays `1.2`.
- **CI job layout.** The compiler job runs the goroutine, channel and Send/Sync
  suites, because they need the pinned compiler; the runtime job keeps the
  compiler-free `libcrug` scheduler and channel checks. The three suite
  wrappers normalise an empty `gcc.sum` count to zero instead of failing with
  `Illegal number` and hiding the missing build.

## [0.2.0] - 2026-10-26

### Added

- **Debian package.** `make package-deb` builds
  `dist/crug_<version>-1_amd64.deb` from the same install tree as the tarball and
  the image, with a `Depends` line measured from the payload (`libc6 (>= 2.38)`,
  `libgcc-s1`, `libstdc++6`, `libzstd1`, `binutils`, `libc6-dev`) and the
  `/usr/bin/crugc`, `/usr/include/crug` and
  `/usr/lib/x86_64-linux-gnu/libcrug.a` links. `make check-DEB` verifies the
  package, and `make check-PACKAGING` installs it with `apt` in a clean
  `ubuntu:24.04` and removes it again.

### Fixed

- **Launcher resolution.** `gcc/crugc` follows its own symlink, so an installed
  `/usr/bin/crugc` runs the packaged compiler instead of silently falling back
  to the host GCC; `make check-launcher` pins the contract.
- **Post-rc1 defect batch (K-003 to K-023).** The audit fixes carry regressions
  and `make check-P1` runs them: capacity-0 channel handoff completes on the
  sender's ticket (R4-RT-001); the channel claim API and `select` lowering
  avoid a shared TLS buffer and readiness probes; task shutdown has a status
  contract, rejects a call from inside a task and refuses spawn during
  draining; frame alignment and overflow are checked; the ownership pass
  rejects reads of an empty owned value; borrows join at CFG joins; alias facts
  require a proven disjoint root; `.crugffi` destructors need exactly one
  matching pointer argument (a non-void return such as `fclose` stays valid)
  and `argN` clauses reject malformed or out-of-range indices; the parser's
  lookahead no longer overflows on chained qualifiers; `c2crug --safe`
  transforms only a proven unconditional release; the exit gates reject a
  fail-open CHECK and replay archived logs. The PAL contract moves to 1.2 for
  `crug_pal_thread_join`; the runtime ABI stays 1 and the `.crugffi` format is
  unchanged. See `docs/crug/RELEASE-NOTES.md` section 8 and
  `docs/crug/KNOWN-ISSUES.md` for the per-defect ledger.

## [0.1.0-rc1] - 2026-10-21

The first release candidate of Crug, a C dialect carried by a pinned GCC 16.2
baseline. Crug is plain C when no Crug flag is given: every extension is opt-in
and the GCC 16.2 testsuite remains the compatibility floor.

This tag is the candidate, not the 1.0 release: the reliability window of
[docs/crug/RELEASE-POLICY.md](docs/crug/RELEASE-POLICY.md) section 4 is still
filling, so milestone M23 is open and the stable `[0.1.0]` entry will be cut
from the same tree once the window and the phase-22 gate are green.

### Added

- **The language.** Contextual `own`, `borrow`, `scope`, `go`, `chan`,
  `move`, `drop`, `unsafe` and `raw` forms that stay valid C identifiers
  where C allows them; deterministic drop at scope exit; move checking that
  rejects implicit owned copies.
- **Borrow checking.** Non-lexical lifetimes with move/drop rejection while an
  incompatible borrow is live, borrow-splitting rules, and diagnostics that
  name the conflicting borrow.
- **Runtime.** `libcrug` tasks, scopes, `go` statements, buffered and
  unbuffered channels with select, deterministic shutdown, and guard-page
  stacks; PAL contract version 1.1 for Linux (x86-64 and AArch64), Windows
  x86-64 and macOS.
- **C interoperability.** `.crugffi` metadata for owned, borrowed and raw
  boundary types, with the FFI version reported by the driver.
- **Diagnostics.** Stable diagnostic identifiers, machine-readable output and
  SARIF export, with a checked golden corpus and a documented catalogue of
  common errors and their fixes.
- **Tooling.** `gcc/crugc` driver (seven-line `--version`), strict mode,
  feature-gate flags, and the kernel codegen comparison against a reference
  build.
- **Quality.** The GCC testsuite plus Crug suites, fuzz harnesses for the
  compiler and the runtime, an ASan/UBSan suite, concurrency benchmarks, and
  the documentation-set, diagnostic-golden and regression-ledger gates.
- **Documentation.** The 19-page `docs/crug/` set covering the language,
  grammar, ownership, borrowing, unsafe, C FFI, ABI, kernel compatibility,
  freestanding mode, concurrency, channels, portability, PAL, optimisation,
  testing, compiler internals, migration, the release policy and the
  compatibility policy.
- **Packaging.** `scripts/package.sh` builds the reproducible
  `crug-<version>-linux-x86_64.tar.gz` install tree (0.1.0 in this release),
  the SPDX 2.3 SBOM `crug-<version>.spdx.json` and `SHA256SUMS`; the multi-stage
  `packaging/Dockerfile` builds the `crug/pkg:0.1.0` image; and
  `docs/crug/BUILD-INSTRUCTIONS.md` documents the source, artefact and
  container builds.
