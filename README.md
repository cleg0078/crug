# Crug

**C + Rust + Go = Crug.** Crug is an opt-in extension of GCC 16.2.0 that brings
Rust-style ownership and borrowing, deterministic destruction, checked data
types, Go-style tasks and channels, and C FFI contracts to ordinary C. With no
`-fcrug-*` flag it is the GCC it was built from: the same C programs, the same
object code and the same C ABI.

**Status:** Crug **0.2.1** (2026-10-27). The Linux x86-64 release tarball,
Debian package, SPDX SBOM and container recipe are in place; the runtime ports
are qualified on Linux x86-64/AArch64, Windows x86-64 and macOS arm64. The
0.2.x line carries the post-`v0.1.0-rc1` audit fixes K-003 to K-023. See
[the release notes](docs/crug/RELEASE-NOTES.md) and
[known issues](docs/crug/KNOWN-ISSUES.md).

## Why Crug

- **C first.** No flag means upstream GCC 16.2 behavior. The plain-C parity
  check compiles the corpus with both drivers and compares exit status and
  object bytes; the ABI is unchanged.
- **Opt-in and contextual.** Ownership, borrowing, tasks and checked types are
  enabled one flag at a time. Crug adds no reserved keywords: `own`, `move`,
  `go`, `chan` and the rest remain usable C identifiers.
- **Static and erased.** Ownership and borrow facts are front-end analysis; a
  qualifier adds no hidden field, reference count or helper call. Only the task
  and channel runtime exists at run time.
- **Diagnosed by code.** Feature diagnostics carry stable
  `crug-<AREA>-<NNN>` codes and matching `-Wcrug-*` warnings where applicable;
  SARIF output carries the code as `ruleId`.
- **Measured, not asserted.** Plain-C identity, zero-cost lowering, kernel
  compatibility, fuzzing, sanitizer cleanliness and every feature gate have
  runnable checks, not just prose claims.

## What Crug adds

| Capability | Surface | Enable with | Start here |
| --- | --- | --- | --- |
| Ownership and destruction | `own`, `raw`, `move`, `drop`, `adopt`, `release`, automatic destructors on every exit path | `-fcrug-ownership` | [OWNERSHIP.md](docs/crug/OWNERSHIP.md), [DROP.md](docs/crug/DROP.md) |
| Borrow checking | `ref`, `mut`, `borrow`, `borrow_mut`; many shared or one mutable borrow; last-use lifetimes | `-fcrug-borrow` | [BORROW.md](docs/crug/BORROW.md), [BORROWING.md](docs/crug/BORROWING.md) |
| Strict mode and escape hatches | `raw` pointers and `unsafe { }`; unanalysed operations rejected outside them | `-fcrug-strict` | [STRICT.md](docs/crug/STRICT.md), [UNSAFE.md](docs/crug/UNSAFE.md) |
| Checked data types | `slice<T>`, `option<T>`, `result<T,E>`, payload enums, constructors and `match` | `-fcrug-slices`, `-fcrug-result`, `-fcrug-adt` | [SAFE-TYPES.md](docs/crug/SAFE-TYPES.md) |
| Tasks and channels | `go`, `yield`, `scope`, `chan T`, `channel`, `send`, `recv`, `close`, `select`, and Send/Sync crossing rules | `-fcrug-goroutines`, `-fcrug-channels` | [GOROUTINES.md](docs/crug/GOROUTINES.md), [SCOPE.md](docs/crug/SCOPE.md), [CHANNELS.md](docs/crug/CHANNELS.md), [SEND-SYNC.md](docs/crug/SEND-SYNC.md) |
| C FFI contracts | `.crugffi` sidecars that transfer ownership and borrows through ordinary C calls, with no ABI change | `-fcrug-ffi=FILE`, `-fcrug-ffi-discover` | [FFI.md](docs/crug/FFI.md) |
| Ownership-aware optimization | proven `mut` and `own` parameters as `restrict`, with the caller contract checked | `-fcrug-alias-facts` | [OPTIMIZATION.md](docs/crug/OPTIMIZATION.md), [ADR 0010](docs/crug/adr/0010-noalias-lowering.md) |
| Migration | `c2crug` annotates only proven ownership and reports the rest as raw with evidence | `c2crug` tool | [C2CRUG.md](docs/crug/C2CRUG.md) |
| Kernel mode | `-fcrug-kernel` for freestanding GNU C with no runtime or ownership lowering | `-fcrug-kernel` | [KERNEL-COMPATIBILITY.md](docs/crug/KERNEL-COMPATIBILITY.md) |

`-fcrug-modern` enables the full non-kernel set. `-fcrug-kernel` is
deliberately excluded from it: kernel code is plain GNU C in v1 ([ADR 0017](docs/crug/adr/0017-kernel-ownership-policy.md)).
The complete flag list, their implications and the `__CRUG_*` feature macros
are in [FLAGS.md](docs/crug/FLAGS.md).

### A first ownership example

```c
#include <stdlib.h>

void fill (mut char *buf, int n)
{
  for (int i = 0; i < n; i++)
    buf[i] = 'x';
}

int main (void)
{
  own char *p = malloc (16);
  fill (borrow_mut (p), 16);
  drop (p);
  return 0;
}
```

`-fcrug-borrow` implies `-fcrug-ownership`, so one flag checks the mutable
borrow, the call and the destruction:

    gcc/crugc -O2 -fcrug-borrow fill.c -o fill
    ./fill

### A first concurrency example

```c
static void send_value (chan int out, int v)
{
  send (out, v);
}

int main (void)
{
  chan int ch = channel (1);

  scope {
    go send_value (ch, 42);
  }

  int value = recv (ch);
  return value == 42 ? 0 : 1;
}
```

The complete, linked version of this shape is
[examples/18-channels.c](examples/18-channels.c); `make examples` builds it
against the runtime. A minimal ownership program is
[examples/02-auto-drop.c](examples/02-auto-drop.c), and the rejected examples
show the diagnostics for the unsafe versions.

## Build and first run

### Prerequisites

A Linux x86-64 host with `make`, `gcc`, `g++` (C++14), `flex`, `bison`, `tar`,
`xz`, `curl`, `sha512sum` and `python3`, with about 10 GiB free for the full
build. Check the host before building:

    make check-host

DejaGnu is only needed for the compiler testsuite; `make bootstrap-dejagnu`
installs it under `tools/` without root.

### Build

The repository root is the GCC top level and the fork source is already in
`gcc/`; the build itself is out of tree in `build/obj`.

    make baseline JOBS=$(nproc)
    make build-full JOBS=$(nproc)
    make -C libcrug PAL=linux

`make baseline` verifies the pinned GCC 16.2.0 tarball, fetches GMP/MPFR/MPC/ISL,
configures `build/obj` and builds `all-gcc`. `make build-full` adds the target
libraries needed to link programs. `make baseline` alone is enough to compile
with `-c`, but linking an executable needs the target libraries. The runtime
build produces the task, channel and PAL objects under `libcrug/build/linux/`.

`gcc/crugc` is the driver. It finds `build/obj/gcc/xgcc` itself, so it works
directly from the repository root:

    gcc/crugc --version

It prints the compiler version and the separately versioned language, Crug ABI,
runtime ABI, FFI metadata and PAL versions:

    crugc 0.2.1
    compiler: GCC 16.2.0
    language version: 0.1.0
    Crug ABI version: 1
    runtime ABI version: 1
    FFI metadata version: 1.0
    PAL version: 1.2

Compile and run a plain program:

    gcc/crugc -O2 examples/01-hello.c -o build/hello
    ./build/hello

Compile the ownership example:

    gcc/crugc -O2 -fcrug-ownership examples/02-auto-drop.c -o build/auto-drop
    ./build/auto-drop

To install the compiler under a prefix (may need root; `DESTDIR` is
supported):

    make install PREFIX=/opt/crug

The full source-build and packaging flow, including the release tarball,
Debian package, SBOM and container image, is in
[BUILD-INSTRUCTIONS.md](docs/crug/BUILD-INSTRUCTIONS.md).

## Examples

`examples/` contains self-contained programs, one per feature. Build every
accepted program, the two rejected ones, and the runtime examples with:

    make examples

Binaries land in `build/examples/`, for example `build/examples/hello`,
`build/examples/auto-drop`, `build/examples/borrow`, `build/examples/ffi`,
`build/examples/ex-goroutines` and `build/examples/ex-channels`. The rejected
programs are compiled with `-fsyntax-only`, so the build also proves that
`crug-UAM-001` and `crug-BORROW-002` still fire.

Representative programs:

| File | What it shows |
| --- | --- |
| [01-hello.c](examples/01-hello.c) | plain C through `crugc`, no flag |
| [02-auto-drop.c](examples/02-auto-drop.c) | `own` plus `adopt`: automatic destruction |
| [03-move.c](examples/03-move.c) | `move` transfers ownership to a callee |
| [05-borrow.c](examples/05-borrow.c) | `ref`/`mut` borrowing |
| [07-nll.c](examples/07-nll.c) | last-use lifetimes |
| [10-ffi.c](examples/10-ffi.c) | `.crugffi` `fopen`/`fclose` ownership |
| [11-safe-types.c](examples/11-safe-types.c) | `slice`, `option`, `result` and `match` |
| [13-payload-enum.c](examples/13-payload-enum.c) | payload enums and exhaustive `match` |
| [15-alias-facts.c](examples/15-alias-facts.c) | `-fcrug-alias-facts` and `restrict` |
| [16-goroutines.c](examples/16-goroutines.c) | `go` and `yield` |
| [17-scope.c](examples/17-scope.c) | structured `scope` and joins |
| [18-channels.c](examples/18-channels.c) | channels, owned transfer and `select` |
| [19-send-sync.c](examples/19-send-sync.c) | Send/Sync crossing rules |

`make examples EXAMPLES=path/to/programs OUT=build/examples-mine` applies the
same per-example flags to another directory. [examples/README.md](examples/README.md)
has the exact command for every file.

## Testing and quality

Crug is gate-driven. Each feature has a DejaGnu suite, the plain-C and
zero-cost claims have differential checks, and the release gates are archived
under `docs/crug/gates/`. Common commands:

| Command | What it checks |
| --- | --- |
| `make check-C_COMPAT` | plain C against the baseline compiler, including object identity |
| `make check-OWNERSHIP check-DROP check-BORROW` | ownership, destruction and borrow suites |
| `make check-SAFE_TYPES check-STRICT_UNSAFE check-FFI` | checked types, strict mode and FFI contracts |
| `make check-GOROUTINES check-SCOPE check-CHANNELS check-SEND_SYNC` | task, scope, channel and crossing rules |
| `make check-P1` | the post-audit regression aggregate used by the fix batch |
| `make check-FUZZ check-SANITIZER` | fuzz harnesses and ASan/UBSan/TSan arms |
| `make check-DOCS` | the documentation manifest and required sections |

Use `make check-c` for the C testsuite while changing the compiler. It needs
DejaGnu; install it with `make bootstrap-dejagnu` if `make check-host` reports
`runtest` missing. [TESTING.md](docs/crug/TESTING.md) is the full operator's
guide and [EXIT-GATES.md](docs/crug/EXIT-GATES.md) records every gate and its
evidence.

## Repository layout

| Path | Contents |
| --- | --- |
| `gcc/` | the forked GCC compiler: front end, Crug analysis, lowering and tests |
| `libcrug/` | the runtime: PAL, scheduler, tasks, channels and context switches |
| `docs/crug/` | language reference, ABI, ADRs, diagnostics, gates and release notes |
| `spec/` | normative RFC, master specification and roadmap |
| `examples/` | small runnable programs, one per feature |
| `scripts/` | gate adjudication, feature checks, packaging and release validation |
| `packaging/` | Docker image, Debian metadata and smoke programs |
| `migration/` | the `c2crug` migration fixtures and proof program |
| `kernel/`, `bench/`, `corpus/`, `fuzz/` | kernel corpus, performance matrix, plain-C corpus and fuzz harnesses |

`build/` and `dist/` are generated and ignored; `sources/` holds the downloaded
GCC tarball when the build fetches it.

## Documentation

- [LANGUAGE.md](docs/crug/LANGUAGE.md) - the language map and the five feature
  groups.
- [FLAGS.md](docs/crug/FLAGS.md) - every `-fcrug-*` option, implication and
  feature macro.
- [BUILD-INSTRUCTIONS.md](docs/crug/BUILD-INSTRUCTIONS.md) - source build,
  install tree, Debian package and container image.
- [TESTING.md](docs/crug/TESTING.md) and [EXIT-GATES.md](docs/crug/EXIT-GATES.md) -
  how to test and what each gate requires.
- [progress.md](docs/crug/progress.md) - the detailed phase history and current
  open items.
- [DIAGNOSTICS.md](docs/crug/DIAGNOSTICS.md), [KNOWN-ISSUES.md](docs/crug/KNOWN-ISSUES.md)
  and [RELEASE-NOTES.md](docs/crug/RELEASE-NOTES.md) - error codes, the defect
  ledger and the current release.
- [COMPATIBILITY-POLICY.md](docs/crug/COMPATIBILITY-POLICY.md) - the version and
  platform promises. Architecture decisions are under [docs/crug/adr/](docs/crug/adr/).

Normative reading order:

1. [spec/000-PRECEDENCE.md](spec/000-PRECEDENCE.md) - which document wins.
2. [spec/CRUG_RFC_0001.md](spec/CRUG_RFC_0001.md) - conformance classes, ABI
   and zero-cost promises.
3. [spec/CRUG_MASTER_SPEC_EN.md](spec/CRUG_MASTER_SPEC_EN.md) - syntax,
   semantics and compiler architecture.
4. [spec/CRUG_RUST_MEMORY_GAPS.md](spec/CRUG_RUST_MEMORY_GAPS.md) - the detailed
   ownership model.
5. [spec/CRUG_PRODUCTION_ROADMAP.md](spec/CRUG_PRODUCTION_ROADMAP.md) - phases,
   gates and milestones.

## Compatibility and limitations

- Plain C with no flags is byte-identical to the pinned GCC 16.2 output; the
  C ABI is a hard requirement, not an afterthought.
- The release artefacts target Linux x86-64. Linux AArch64, Windows x86-64 and
  macOS arm64 runtime ports are qualified by the multi-OS checks, but binary
  packages for those hosts are not built yet.
- Ownership and borrow checks are static and erased; the task/channel runtime
  is the only run-time component.
- Kernel code is plain GNU C in v1: no ownership, drop, borrow or race analysis
  applies inside the kernel ([ADR 0017](docs/crug/adr/0017-kernel-ownership-policy.md)).
- Fuzzing is bounded evidence, not a proof of absence; the nightly window and
  its budgets are recorded in [TESTING.md](docs/crug/TESTING.md) and
  [FUZZING.md](docs/crug/FUZZING.md).
- A select send arm is evaluated on each claim pass and in the closed-send abort
  path, so its value expression must be side-effect-free apart from the move;
  an owned send can produce a conservative `OWN-007`/`DROP-006`.
- Full open items and their disposition are in [KNOWN-ISSUES.md](docs/crug/KNOWN-ISSUES.md)
  and [progress.md](docs/crug/progress.md).

## Licence

GPL-3.0-or-later; see [LICENSE](LICENSE) and [NOTICE](NOTICE). The imported
`gcc/` tree keeps its own GPLv3+/LGPLv3+ headers, and `libcrug` is
GPLv3-or-later without a runtime exception for now.