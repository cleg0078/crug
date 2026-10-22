# Changelog

All notable changes to Crug are recorded here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html): language, Crug ABI,
runtime ABI, FFI metadata and PAL versions are versioned separately, as
`gcc/crugc --version` reports.

## [Unreleased]

### Added

- **Debian package.** `make package-deb` builds
  `dist/crug_0.1.0-1_amd64.deb` from the same install tree as the tarball and
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
