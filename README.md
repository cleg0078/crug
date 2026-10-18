# CRUG

C + Rust + Go = CRUG. An extension of GCC that brings Rust-style memory
management and Go-style concurrency to C code, with C ABI compatibility as a hard
requirement.

## Status

Design phase. The repository contains specification documents and architecture
decisions; there is no compiler yet. See [docs/crug/progress.md](docs/crug/progress.md)
for what is decided, what is open, and what to do next.

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
[docs/crug/EXIT-GATES.md](docs/crug/EXIT-GATES.md).

## What Crug adds

- `own`, `ref`, `mut`, `raw` qualifiers with move-only ownership and
  deterministic destruction, without changing C layout or calling conventions.
- Borrow checking (N shared or one exclusive) enforced at compile time.
- `go`, `scope`, `chan`, `select` for structured concurrency with explicit
  ownership transfer on send.
- `-fcrug-kernel` for freestanding use with no hidden runtime at all.

## Building

The fork tracks GCC 16.2.0, pinned in [docs/crug/GCC-BASELINE.md](docs/crug/GCC-BASELINE.md)
by checksum. The top-level [Makefile](Makefile) reproduces that baseline:

    make baseline      # download, verify, extract, fetch prerequisites, configure, build
    make help          # every target
    make check-host    # required host tools, read-only

The first run downloads the 102 MiB tarball, unpacks it under `build/`, fetches
GCC's own GMP/MPFR/MPC/ISL prerequisites, and builds `all-gcc` out of tree into
`build/obj`. Later runs are incremental. `make baseline` stops at the compiler
binaries: `xgcc` compiles (`-S`, `-c`) but linking needs the target libraries
built by `make build-full`. `make install PREFIX=...` installs the compiler, and
`make check-c` runs `gcc.dg` (it needs dejagnu). Nothing generated is tracked.
See [NOTICE](NOTICE) for the licensing consequence of deriving from GCC.

## Licence

Specification text: MIT (see [LICENSE](LICENSE)). Compiler code derived from GCC
will be GPLv3-or-later; the runtime library licence is undecided. See
[NOTICE](NOTICE).
