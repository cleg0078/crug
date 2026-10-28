# Changelog

All notable changes to Crug are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html): language, Crug ABI,
runtime ABI, FFI metadata and PAL versions are versioned separately, as
`gcc/crugc --version` reports.

## [Unreleased]

## [0.2.4] - 2026-10-28

### Added

- **Raytracer demo.** `bench/raytracer/` is a single-file Crug port of the
  Go/X11 ray tracer that spawns one goroutine per pixel: 480,000 tasks, a
  struct-element result channel of capacity 8192, X11 confined to the main
  thread, and the original stat section (spawn cost, throughput and
  `/proc/self/status` memory) translated to the Crug runtime counters.
- **Receive into caller storage.** `crug_chan_recv_into` and
  `crug_chan_recv_into_owned` copy a received element into storage the caller
  owns (size checked against the element size, aligned, 1 on a value and 0 on a
  closed, drained channel) and never touch the shared scratch. `recv` lowers to
  them for every non-VLA element, which removed a misaligned load on a
  64-byte-aligned element. The pointer forms stay for compatibility (K-030,
  K-032).
- **Regression tests.** The scope wake race (R-01), the `queue_peak` data race
  (R-02), the startup/shutdown handshake (S-01), the reusable receive scratch
  (A-01), the aligned channel receive (`channels-recv-align`), the owned-receive
  ownership cases, the FFI truncation cases and the `c2crug` preprocessing
  witnesses each have a program or fixture.

### Changed

- **Ownership joins.** A `drop` on one arm of a conditional or `&&`/`||`
  expression, a `switch` whose arms all release, and `drop` of a value received
  from an owning channel are accepted (K-024, K-025, K-041); the alias and
  borrow leads are closed too: an indirect store invalidates an escaped pointer
  (`crug-BORROW-024`), an unrelated borrow at a join is no longer rejected, and
  a `move` into an `own` parameter is proven disjoint.
- **FFI metadata.** A NUL byte inside the file is `crug-FFI-012`, the entry cap
  is `crug-FFI-010`, truncated metadata reports `crug-FFI-003` at the right
  column instead of reading past the token buffer, and `owned` is optional on a
  `returns` clause (ADR 0007 A-3). Two truncation cases join the suite, now
  thirty cases (K-026 to K-029).
- **Runtime.** A scope tracks its children with an `in_flight` handshake, so a
  group that finishes while the parent waits no longer loses a wake; `queue_peak`
  is atomic; runtime start and shutdown run one handshake, so a concurrent start
  and shutdown cannot leave a half-built worker set (K-033 to K-035). The public
  task API and the Crug runtime ABI stay 1.
- **`c2crug --safe` reads preprocessed source.** Eligibility is decided on
  `cc -E` output and mapped back through the `#line` markers, so a macro that
  hides an aliasing free or a second exit can no longer look eligible; the
  sidecar must start with a `crugffi 1.<minor>;` header, and per-function facts
  are cached (K-037 to K-040).
- **Coverage, gate and CI tooling.** `run-coverage.sh` records the commit it
  measures and has a self-test, `gate.sh` escapes regular-expression
  metacharacters in a marker, and `check-ci-gate.sh` selects the latest run of
  the required workflows only (K-036).
- **Docs, ledger and gate.** K-024 to K-042 join the ledger (K-042 is open: the
  diagnostic scripts resolve `crugc` from `PATH` before the in-tree launcher),
  and amendment A-33 re-baselines the counts the batch moved.
- **Release version.** `VERSION` and `crugc --version` now report `0.2.4`.
  The language version stays `0.1.0`, the Crug and runtime ABIs stay `1`,
  `.crugffi` stays `1.0` and the PAL contract stays `1.2`.

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
