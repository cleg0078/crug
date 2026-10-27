# Benchmarks

This directory holds the CRUG benchmark corpus and its driver. Phase 11
(ownership-aware optimization) populates the first matrix; Phase 21
(performance qualification) extends it with the parity harness.

## The three-arm alias matrix

`bench/kernels/` has four kernels: `vadd`, `stencil`, `strcopy` and
`list`. Each kernel is split into

- `<kernel>_kernel.c` - the loop, compiled three ways with
  `-DBENCH_ARM=0|1|2`: plain C pointers, C + hand-written `restrict`, and
  Crug `mut`/`ref` parameters with `-fcrug-borrow -fcrug-alias-facts`;
- `<kernel>_main.c` - one plain-C `main` shared by all three arms, which
  allocates the buffers, times the kernel and prints
  `<name> <min-ns> <checksum>`.

The shared main is what makes the matrix meaningful: because the Crug ABI is
the C ABI, the three executables differ only in the kernel translation unit, so
the Crug and restrict kernel objects can be required to be **byte-identical**.
That identity is the primary evidence of Phase 11; the wall-clock ratio is
reported as secondary evidence. See `docs/crug/OPTIMIZATION.md`, ADR 0010 and
`EXIT-GATES.md` phase 11.

## Driver

    bench/run.py --matrix alias --check     # 5% band, identity, timings
    bench/run.py --matrix alias --lto       # all arms build and agree under -flto
    bench/run.py --record-baseline          # write bench/baseline.json
    bench/run.py --plain-c --check          # plain C not degraded

`CRUGC` selects the compiler (default `<repo>/gcc/crugc`). Build artefacts
go to `build/bench/` and results to `bench/results/`; both are gitignored.
Only `bench/baseline.json` (the recorded plain-C object digests and timings)
is committed.

## Phase 21 qualification

```
bench/run.py --zero-cost --check       # Crug construct vs plain-C twin
bench/run.py --pristine --check        # object parity with unmodified GCC 16.2
bench/run.py --plain-c --check         # no drift against bench/baseline.json
bench/run.py --concurrency --check     # tasks, memory per task, channels
bench/run.py --kernel --check          # kernel corpus codegen
bench/run.py --performance --check     # all five, report in bench/results/<date>.md
```

- **Zero cost** (`bench/zero-cost/<case>/{crug.c,plain.c}`): `own-drop`,
  `move`, `ref-param` and `mut-param`, each compiled at `-O0`/`-O2`/`-O3` from
  the same basename. The driver requires an identical optimised tree dump and
  an identical instruction stream (local labels and temporary numbers are
  canonicalised); `-fdump-tree-gimple` is deliberately not used, because its
  initial dump differs cosmetically for a lowered drop.
- **Pristine parity** builds the unmodified tarball compiler with
  `make pristine-xgcc` (configure and build in `build/pristine-obj`) and
  requires the objects of `corpus/*.c` and `bench/kernels/*.c` to be byte
  identical. This is the only non-tautological plain-C comparison: the phase-1
  `diff-cc.sh` compares two builds of the same fork.
- **Concurrency and channels** live in `bench/concurrency/run.py` and
  `bench/channels/`; the arms are pthread-per-task, an allocation-free C
  thread pool and the Crug runtime.
- **Kernel codegen** is `scripts/check-kernel-perf.sh` over the 438-object
  corpus of `kernel/KERNEL-CORPUS.md`; the one allowed difference
  (`init/version-timestamp.o`) is explained in `kernel/object-diff-allow.txt`.
- The numbers these modes enforce are ratified in
  `docs/crug/RELEASE-POLICY.md` Section 2; the measured results and the
  methodology are in `docs/crug/PERFORMANCE.md`.

## Raytracer demo

`bench/raytracer/` holds a single-file Crug port of the Go/X11 ray tracer: one
task per pixel over 800x600, a struct-element result channel of capacity 8192,
X11 confined to the main thread, and the original stat section (spawn cost,
throughput, `/proc/self/status` memory). It is a demonstration, not a gate
arm; build and run instructions are in `bench/raytracer/README.md`.

## Rules

- Each input records its provenance and licence in `bench/NOTICE`; per-input
  licences differ, so there is no blanket licence for this directory.
- No benchmark is vendored from a project whose licence forbids it (in
  particular the GPLv2 Linux kernel source is consumed from `sources/`, never
  copied here).
- Results are written under `bench/results/`, which is gitignored; only the
  driver, the corpus index and the recorded baseline are committed.
- A timing band is only meaningful when the compiled code differs. If two arms
  produce byte-identical kernel objects, their measured ratio is reported as
  1.000; this host swings by more than a factor of two between runs of the same
  executable.
