# Performance Qualification

**Status:** Measured on 2026-10-20 for M21. The thresholds this document is
measured against are ratified in `docs/crug/RELEASE-POLICY.md` Section 2; the
numbers are evidence, not policy. Generated report for any run:
`bench/results/<date>.md` (gitignored). The Phase 21 gate passes 6/6 at
`690dd8f` (logs under `docs/crug/gates/phase-21/690dd8f/`); the numbers
below are from that run.

The goal is narrow and falsifiable: prove that Crug constructs **cost nothing**
over the C a project would have written, that **plain C compiles exactly as
before**, that the runtime's task and channel primitives are **of the right
order**, and that the kernel corpus **still produces the same machine code**.
Nothing here claims Crug is faster than C; it claims Crug does not make C
slower.

## 1. Method and hardware

| item | value |
|---|---|
| host | Linux x86-64, 20 logical CPUs |
| Crug compiler | `gcc/crugc` wrapping the tree's `build/obj/gcc/xgcc` (GCC 16.2.0) |
| reference compiler | unmodified GCC 16.2.0 built from the pinned tarball by `make pristine-xgcc` (out of tree in `build/pristine-obj`) |
| kernel corpus | 438 objects, `kernel/KERNEL-CORPUS.md` |
| timing rule | minimum of repeated runs: best of 7 internal repetitions per process, best of 3 processes for the recorded baseline, best of 8 for the concurrency arms; the concurrency driver also takes three whole-run shots and merges the minima |
| report | `make check-PERFORMANCE` runs all five checks and writes `bench/results/<date>.md` (the measured run is `bench/results/2026-10-20.md`) |

The host is shared and noisy. Absolute timings therefore carry a wide margin;
object identity is what is enforced, and timing bands are secondary guards.

## 2. Zero cost - construct versus plain-C twin

`bench/run.py --zero-cost --check` compiles each case twice from the same
basename (so debug and assembly names match) and compares the optimised tree
dump and the instruction stream. Local labels and temporary numbers are
canonicalised before comparing.

Instruction counts at `-O0` / `-O2` / `-O3`, Crug twin first:

| case | Crug construct | plain-C twin | instructions (Crug) | instructions (plain) |
|---|---|---|---|---|
| `own-drop` | owned allocation, automatic drop | guarded `free` | 18 / 1 / 1 | 18 / 1 / 1 |
| `move` | `move (p)` of an owned parameter | plain assignment | 9 / 2 / 2 | 9 / 2 / 2 |
| `ref-param` | `ref const int *` sum loop | `const int *` sum loop | 21 / 12 / 48 | 21 / 12 / 48 |
| `mut-param` | `mut int *` fill loop | `int *` fill loop | 20 / 17 / 42 | 20 / 17 / 42 |

**Result: 12 of 12 comparisons identical in the optimised tree dump and in the
instruction stream; 0 added instructions, 0 added calls; "zero-cost: OK".**
`own-drop` at `-O2` and `-O3` collapses to a single instruction
because both twins' allocations are dead; the point is that the drop lowering
does not keep anything alive that plain C would have removed.

Two measurement notes:

- `-fdump-tree-gimple` is deliberately not used. For a lowered drop its
  initial dump differs cosmetically (one extra empty label/goto) even when the
  optimised code is identical, so it would measure lowering noise.
- `-fdump-tree-optimized` at `-O0` still numbers temporaries
  (`D.2968` versus `D.2969`), so `normalize_tree` blanks temporary numbers
  before the comparison.

## 3. Plain-C codegen parity

### 3.1 Against the unmodified GCC 16.2

`bench/run.py --pristine --check` compiles every `corpus/*.c` program and
every `bench/kernels/*.c` translation unit with the pristine compiler and
with `gcc/crugc` at `-O0`, `-O2` and `-O3`.

**Result: 114 source/option pairs, 0 object differences** - every object is
byte-identical to what unmodified GCC 16.2 emits. The `-O2 -flto` kernel
executables were byte-identical too, so no LTO timing band was needed
(that band exists only for hosts where LTO links are not reproducible).

This is the only non-tautological plain-C comparison in the project: the
phase-1 `scripts/diff-cc.sh` compares two builds of the same fork and can
only prove the fork is deterministic, not that it changed nothing.

### 3.2 Against the recorded baseline

`bench/run.py --plain-c --check` compares the same programs with
`bench/baseline.json`.

**Result: every non-LTO object is byte-identical**; the recorded timing ratios
spread from 0.916 to 1.012, which is exactly the host noise the policy expects
and why the policy enforces object identity and uses the 1.02 band only when an
object actually changed. The LTO rows measured 1.056-1.236 and agreed on
results; those ratios are informational.

## 4. Task dispatch and memory per task

`bench/concurrency/run.py --check` runs 200 tiny tasks in three arms: a
pthread per task, an allocation-free C thread pool (cold and warm), and the
Crug runtime.

| arm | dispatch time | per-task memory (VmSize delta) |
|---|---|---|
| pthread per task | 2.24 ms | 8032.1 KiB |
| C thread pool (cold) | 127 us | - |
| C thread pool (warm) | 23-37 us | - |
| Crug runtime | 0.186 ms | 67.7 KiB |

**Result: ratio Crug/pthread = 0.083 (threshold 1.5), tasks 200, lost 0**, and
**memory per task 67.7 KiB versus 8032.1 KiB (ratio 0.0084, threshold 0.5)**.
Crug is an order of magnitude faster than spawning a pthread per task and uses
about two orders of magnitude less memory, because a spawn only queues the task
and the worker reuses a 64 KiB owned stack rather than an 8 MiB thread stack
(ADR 0008 A-3).

The allocation-free pool wins dispatch by about 1.5x; that ratio is recorded
(`pool-ratio=1.466`) and **not gated**, because a pool is not the baseline a
project starts from and a 64 KiB stack per task is a design decision of the
runtime (ADR 0008), not an overhead of the language.

A bounded allocation cache (ADR 0008 A-2) bounds the dispatch path, and since
ADR 0008 A-3 a spawn is queue-only, so this probe measures the queue and the
workers' first-run stack materialization. The probe is
`make -C libcrug PAL=linux bench-tasks`; it is not a gate because CI runners
vary. With 1,000,000 no-op tasks and 16 KiB stacks:

| workers | elapsed | ns/task | peak VmPeak |
|---|---|---|---|
| 1 | 0.310 s | 310 | 142 MB |
| 2 | 0.543 s | 543 | 216 MB |
| 4 | 0.975 s | 975 | 396 MB |
| 8 | 1.524 s | 1524 | 658 MB |

The 0.5 s target is met on one worker; the multi-worker results are queue-lock
bound (the spawn loop is the whole run: `spawn_ns` and `ns` differ by under
1%), not allocation bound, and are recorded as the remaining scaling limit.
The 100,000-task stress run drops from 0.71 s with the cache disabled to 0.17 s
with it enabled on this host.

### 4.1 Burst spawn

A program that spawns every task before draining them - a raytracer's
`go render_pixel (...)` per pixel is 480,000 tasks - used to pay a stack
reservation for every queued task. Since ADR 0008 A-3 a spawn only copies the
frame and queues the task; the dispatching worker materializes the stack. On
this host, 480,000 tasks with 64 KiB stacks and an 8192-slot channel:

| shape | before | after |
|---|---|---|
| spawn all, 8 workers | 7.36-11.39 s, VmPeak 32.0 GB | 0.53-0.65 s, VmPeak 1.5-1.8 GB |
| spawn all, 1 worker | 3.72-3.79 s, VmPeak 31.5 GB | 0.149 s, VmPeak 689 MB |
| window 8192, 8 workers | 1.05-1.08 s, VmPeak 645 MB | 0.81-0.88 s, VmPeak 581-644 MB |

The spawn loop alone costs 91 ns/task on one worker, and a queued task holds one
task header and no stack, so the address space scales with the workers and the
cache window, not with the burst. The window rows are a probe, not part of
`bench/concurrency` (whose arms stay at 200 tasks).

## 5. Channels

| measurement | value | threshold |
|---|---|---|
| unbuffered ping-pong round trip | 197 ns | < 10 us |
| throughput, capacity 1024 | 42.5M int elements/s | > 1M elements/s |
| memory per channel | 4224 B (4096 ring + 128 overhead) | < 64 KiB |
| scaling, 1/2/4 pairs | 41.4 / 41.2 / 40.6M elements/s | recorded only |

`recv` now copies directly into the receiving task's own storage through
`crug_chan_recv_into` / `crug_chan_recv_into_owned`: there is no scratch
hand-off and no shared buffer on the receive path. The receive scratch cache
only backs the legacy pointer forms and is bounded at 64 KiB per task. The
numbers above are kept from the recorded qualification run.

Every arm agreed on the checksum, 2048 channels passed their own checksum, and
no element was lost or reordered. Scaling is recorded rather than gated because
this host is shared and oversubscribed; a 1/2/4-pair ratio near 1.0 means the
channel fast path is not the bottleneck, not that it parallelises.

## 6. Kernel codegen

`scripts/check-kernel-perf.sh` compares the 438-object corpus of
`kernel/KERNEL-CORPUS.md` built by `gcc/crugc` with the same objects built by
the wrapped driver.

| check | result |
|---|---|
| byte-identical objects | 436 of 438 |
| explained differences | `init/version-timestamp.o` (its `UTS_VERSION` embeds build time and build number) and `init/version.o` (the `$CC --version` identification that `scripts/mkcompile_h` copies: `crugc 0.1.0` for the launcher against `xgcc (GCC) 16.2.0` for the reference driver); both reasons are in `kernel/object-diff-allow.txt`, and the two `.config` files differ only in `CONFIG_CC_VERSION_TEXT`, which the script filters |
| disassembly | 0 of 438 differ |
| `.text` size | 0 of 438 differ (3,580,580 bytes total) |
| symbols | 43,309 total (26,798 defined, 16,511 undefined), 0 objects differ |
| corpus bytes | 12,173,712 |
| full build | vmlinux 51,420,536 bytes, bzImage 14,783,488 bytes |

The corpus carries no `.stack_sizes` section, so the stack-usage arm reports
"no `.stack_sizes` section in the corpus objects"; the script's handling of
that section is exercised separately on `objcopy --add-section` copies.

## 7. What this does not claim

- **Not faster than C.** Every result above is "same machine code" or "within
  the ratified band"; there is no "Crug beats C" target (MASTER_SPEC section 26).
- **Not a comparison against the pool.** Dispatch and memory are compared with
  pthread-per-task, the baseline a C project actually starts from.
- **Not an optimisation pass.** Phase 21 measures and signs off; it introduces
  no new optimisation.
- **Not machine-independent numbers.** The bands are; the absolute figures are
  this host's.

## 8. Reproduction

`make pristine-xgcc` once (builds the reference compiler), then
`make check-PERFORMANCE`, or run the five checks separately:

```
bench/run.py --zero-cost --check
bench/run.py --pristine --check
bench/run.py --plain-c --check
bench/run.py --concurrency --check
scripts/check-kernel-perf.sh
```
