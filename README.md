# CRUG

C + Rust + Go = CRUG. An extension of GCC that brings Rust-style memory
management and Go-style concurrency to C code, with C ABI compatibility as a hard
requirement.

## Status

Design phase. The repository contains specification documents, architecture
decisions, and the pinned GCC 16.2.0 tree that the fork modifies (the
repository root is the GCC top level; the compiler is `gcc/`; the import is
tagged `releases/gcc-16.2`). There is no Crug compiler yet. See
[docs/crug/progress.md](docs/crug/progress.md) for what is decided, what is
open, and what to do next.

## Reading order

1. [spec/000-PRECEDENCE.md](spec/000-PRECEDENCE.md) - which document wins when
   they disagree, plus the conflict register.
2. [spec/CRUG_RFC_0001.md](spec/CRUG_RFC_0001.md) - conformance classes, invariants,
   ABI and zero-cost promises.
3. [spec/CRUG_MASTER_SPEC_EN.md](spec/CRUG_MASTER_SPEC_EN.md) - syntax, semantics,
   compiler architecture.
4. [spec/CRUG_RUST_MEMORY_GAPS.md](spec/CRUG_RUST_MEMORY_GAPS.md) - the detailed
   ownership model that must be merged into the normative text.
5. [spec/CRUG_PRODUCTION_ROADMAP.md](spec/CRUG_PRODUCTION_ROADMAP.md) - phases,
   gates, milestones.

Architecture decisions live in [docs/crug/adr/](docs/crug/adr/); release gates in
[docs/crug/EXIT-GATES.md](docs/crug/EXIT-GATES.md). Diagnostic codes are registered
in [docs/crug/DIAGNOSTICS.md](docs/crug/DIAGNOSTICS.md), and
`scripts/check-diagnostics.sh` keeps the registry consistent with the docs.

## What Crug adds

- `own`, `ref`, `mut`, `raw` qualifiers with move-only ownership and
  deterministic destruction, without changing C layout or calling conventions.
- Borrow checking (N shared or one exclusive) enforced at compile time.
- `go`, `scope`, `chan`, `select` for structured concurrency with explicit
  ownership transfer on send.
- `-fcrug-kernel` for freestanding use with no hidden runtime at all.

## Building

The fork tracks GCC 16.2.0, pinned in [docs/crug/GCC-BASELINE.md](docs/crug/GCC-BASELINE.md)
by checksum and imported as the tag `releases/gcc-16.2`. The top-level
[Makefile](Makefile) reproduces that baseline:

    make baseline      # verify the tarball, fetch prerequisites, configure, build
    make help          # every target
    make check-host    # required host tools, read-only

The source tree is already in the repository, so a build does not unpack the
tarball: `make baseline` fetches GCC's own GMP/MPFR/MPC/ISL prerequisites (they
unpack at the top level and are ignored) and builds `all-gcc` out of tree into
`build/obj`. Later runs are incremental; after a source change use
`make reconfigure` when configure inputs changed. `make baseline` stops at the
compiler binaries: `xgcc` compiles (`-S`, `-c`) but linking needs the target
libraries built by `make build-full`. `make install PREFIX=...` installs the
compiler. `make check-c` runs the `gcc.dg` C testsuite; it needs DejaGnu, which
`scripts/bootstrap-dejagnu.sh` installs into `tools/` without root. Nothing
generated is tracked.
See [NOTICE](NOTICE) for the licensing consequence of deriving from GCC.

## Licence

GPL-3.0-or-later (see [LICENSE](LICENSE)). The imported `gcc/` tree keeps its
own GPLv3+/LGPLv3+ headers, and the runtime library is GPLv3-or-later without a
runtime exception for now. [NOTICE](NOTICE) has the per-path map and the
historical note about the initial MIT commit.
