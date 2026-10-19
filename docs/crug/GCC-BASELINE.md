# CRUG GCC baseline and build environment

Status: Proposed, 2026-10-18. This file pins the external inputs that every
gate in `docs/crug/EXIT-GATES.md` assumes. Phase 0's first check reads this
filename, so the baseline must stay here rather than in a general notes file. Nothing here is a language decision;
changing a pin is a build-infrastructure change and must be a separate commit
with the old and new values quoted.

## 1. Compiler baseline

The fork tracks upstream GCC 16.2, released 2026-08-07. The tarball tree is
imported into this repository by the commit tagged `releases/gcc-16.2`, so the
repository root is the GCC top-level directory (top-level `configure`,
`contrib/`, and the compiler proper in `gcc/`).

| Item | Value |
|------|-------|
| Source | https://gcc.gnu.org/pub/gcc/releases/gcc-16.2.0/gcc-16.2.0.tar.xz |
| Release date | 2026-08-07 |
| Size | 102 MiB (tar.xz) |
| SHA512 | `c51c30ca7422d0cbecf504b2e0f33c3aca31e0f90a76b65217f465163fa6fa17b3f5de39e145c47e5bab90ac0ce7fff3b03c8d553ae36e01faaea5a50f8648d1` |
| Signature | `gcc-16.2.0.tar.xz.sig` (detached, also published) |
| Fork base in git | annotated tag `releases/gcc-16.2`, pointing at the import commit |

The checksum above was read from upstream `sha512.sum` on 2026-10-18 and
recorded here so that the import commit can be checked without network access.
Verification is automated: `make download` fetches the tarball into `sources/`
and `make verify` runs `sha512sum --check --strict` against
`docs/crug/gcc-baseline.sha512`. Moving to a newer release is described in
section 7 and measured in `docs/crug/GCC-UPGRADE.md`. The standalone equivalent is:

```sh
cd sources
curl -fSLO https://gcc.gnu.org/pub/gcc/releases/gcc-16.2.0/gcc-16.2.0.tar.xz
curl -fSLO https://gcc.gnu.org/pub/gcc/releases/gcc-16.2.0/gcc-16.2.0.tar.xz.sig
sha512sum -c ../docs/crug/gcc-baseline.sha512
gpg --verify gcc-16.2.0.tar.xz.sig gcc-16.2.0.tar.xz   # key per https://gcc.gnu.org/develop.html
```

`make baseline` runs download, verify, extract, prerequisite fetch, configure and
`all-gcc` in that order; it is the reproducible entry point the phase 0 gate
calls. When the tree has been imported, `make extract` is a no-op and `make`
builds out of tree from the repository root; the checksum remains the source of
truth for what the import must contain.

Do not substitute a git checkout of upstream for the tarball in a gate: the
tarball is the artefact the checksum and signature cover.

## 2. Host toolchain to build the fork

The values below are what a Phase 0 bootstrap requires; the first Phase 0 task
is to record the exact versions actually used.

| Item | Minimum | Note |
|------|---------|------|
| Host C++ compiler | C++14 | GCC 12+ recommended; needed to build the stage-1 compiler |
| GMP | 4.3.2 | build prerequisite |
| MPFR | 3.1.0 | build prerequisite |
| MPC | 1.0.1 | build prerequisite |
| isl | 0.15 | optional, needed for graphite |
| zstd | 1.4 | optional compression support |
| Python | 3.8 | build scripts |
| flex, bison | current | only if .l/.y files change |
| DejaGnu, Expect, Tcl | 1.6.3, 5.45.4, 8.6 | testsuite only; `scripts/bootstrap-dejagnu.sh` installs the Debian packages under `tools/` without root and writes `tools/dejagnu.env`, which `make check-c` sources |

Configure baseline for development (no bootstrap, assertions on):

The Makefile runs exactly this from `build/obj` against the source tree, which is
the repository root once the tarball has been imported (top-level `configure`,
compiler in `gcc/`) and `build/gcc-16.2.0` otherwise; `make print-config` prints
the flags and `make baseline` runs them. Prerequisites are unpacked at the top
level by `contrib/download_prerequisites` and are ignored by git.

```sh
mkdir -p build/obj && cd build/obj
../configure \
  --prefix=/opt/crug \
  --enable-languages=c,c++ \
  --disable-bootstrap \
  --disable-multilib \
  --enable-checking=yes \
  --with-arch=x86-64 --with-tune=generic
```

`--disable-multilib` because CRUG targets one triple per build and a host without
32-bit development libraries otherwise fails configure's multilib probe.
`--with-arch` takes the GCC CPU name `x86-64`, not the triple fragment `x86_64`:
the latter is rejected while configuring `gcc/`.

`--enable-languages` gains `crug` when the frontend exists; until then the fork
is a patched C frontend and the language name is not accepted by configure.

## 3. Target triples under test

| Role | Triple | Gates |
|------|--------|-------|
| Primary development host | x86_64-pc-linux-gnu | all phases |
| Secondary target | aarch64-linux-gnu | Phase 8 onwards, cross via qemu-user for tests |
| Kernel target | x86_64 and aarch64, freestanding | Phase 19 |

A triple not listed here is untested; adding one needs a gate entry.

## 4. Kernel baseline: PINNED

`docs/crug/EXIT-GATES.md` Phase 2 and Phase 19 require a fixed kernel version,
defconfig and boot harness. They are pinned in `docs/crug/KERNEL-BASELINE.md`
(conflict C-09 in `spec/000-PRECEDENCE.md`, resolved 2026-10-18):

- Linux 6.18.57 LTS, tag `v6.18.57`, commit
  `2ff600d29ee478d0a8d46ef8a58afb5e038fe692`, tarball digest in
  `docs/crug/kernel-baseline.sha256` (`scripts/kernel-pin.sh --verify`);
- `arch/x86/configs/x86_64_defconfig`, no fragment;
- the qemu boot command and the two console markers;
- the subsystem scope; the per-object corpus list that Phase 19 needs is still
  missing (`kernel/KERNEL-CORPUS.md`), so `class K0` remains unclaimable until
  that file exists.

The kernel source is GPLv2 and is never vendored: it lives under `sources/`
(ignored) and only the digest enters the repository.

## 5. Repository-derived inputs

| Item | Source of truth |
|------|-----------------|
| Language decisions | `docs/crug/adr/` (index in `docs/crug/adr/README.md`) |
| Conflict resolution | `spec/000-PRECEDENCE.md` |
| Gate definitions | `docs/crug/EXIT-GATES.md` |
| Licences | `NOTICE` (project files and libcrug GPL-3.0-or-later; imported GCC files keep their own headers) |

## 6. Update policy

1. A pin change is its own commit titled `crug: build: ...` quoting old and new.
2. A pin change that invalidates a gate archive must re-run that gate.
3. Never record a checksum that was not verified against the published
   `sha512.sum` or the signed artefact at the time of the commit.
4. A baseline upgrade follows ADR 0022 and `docs/crug/GCC-UPGRADE.md`; a major
   upgrade supersedes ADR 0001 with a new ADR.

## 7. Upgrading the baseline

ADR 0022 (Proposed) classifies a baseline change as a re-pin (P), a minor upgrade
(M) or a major upgrade (J) and fixes the evidence for each.
`docs/crug/GCC-UPGRADE.md` is the measured procedure: what the fork actually
touches (9 upstream files, 32 hunks), the porting map, eight behavioural pins,
the seven-step upgrade, the 31-check acceptance matrix and the effort model. The
tools are `scripts/fork-delta.sh` (offline delta against the vendor tag),
`scripts/check-gcc-drift.sh` (drift against a given upstream revision; unknown
drift blocks a merge) and `scripts/verify-upgrade.sh` (the matrix; `--gates`
adds phases 0-9). `make fork-delta`, `make check-gcc-drift` and
`make verify-upgrade` wrap them.

Do not edit this file to describe a newer version. An upgrade changes the pin,
the import tag and the version the build reports; `make xgcc-version` must print
the same version the pin names, and the changelog below records old and new
together.
