# Concurrency and channel benchmarks (Phase 21)

Plain C arms for the Crug task runtime and channel implementation, plus a
Python 3 (stdlib only) driver.  The programs live in this directory and in
`bench/channels/`:

| program | modes | measures |
| --- | --- | --- |
| `dispatch.c` | `pthread pool pool-warm crug` | time to start 200 task bodies and wait for them: pthread-per-task, a fixed 8-worker pthread pool, and `crug_task_spawn` + `crug_task_runtime_shutdown` |
| `memtask.c` | `pthread crug` | `/proc/self/status` VmSize delta with 200 task bodies parked on a gate (reserved bytes per task) |
| `../channels/chanbench.c` | `pingpong throughput memory scaling` | unbuffered round-trip latency, buffered element throughput at capacity 1024, bytes per idle channel, and aggregate throughput with 1/2/4 channel pairs |

Every program prints one machine line per arm, `<name> <mode> key=value ...`,
after taking the minimum of eight internal runs (`*_RUNS 8`).  The driver
runs each program three times and keeps the smallest observed value; if a
check fails it takes three more shots and judges the repeated minima, so one
unlucky scheduling slot on a loaded host cannot fail the gate.

## Build and run

    make -C libcrug PAL=linux          # once; the driver does this if needed
    bench/concurrency/run.py --json build/bench/concurrency/results.json --check

The driver compiles the arms with

    cc -O2 -std=c11 -Wall -Wextra -Wno-unused-function \
       -I bench -I libcrug/core <source> \
       libcrug/build/linux/{task,pal_linux,switch,stack,chan}.o -pthread

into `build/bench/concurrency/` (gitignored), forces
`CRUG_TASKS_WORKERS=8` for every child, and removes `CRUG_TASKS_STACK` so
all arms are comparable on every host.  `--check` is accepted for the
`bench/run.py` interface; the bands below are always enforced.

Exit status: `0` all checks pass, `1` a check failed, `2` the compiler or
the runtime objects are unavailable (environment problem).

## Bands (ratified by the Phase 21 release policy)

* **dispatch, hard:** 200 tasks, every arm runs every body exactly once,
  `lost == 0`, and `crug <= 1.5 x pthread-per-task`.
* **memory, hard:** `crug bytes/task <= 0.5 x pthread bytes/task` (VmSize
  delta; pthread's default 8 MiB stack versus Crug's 64 KiB stack plus a
  4 KiB guard page).
* **channels, hard:** every value received in order with no loss, unbuffered
  round-trip latency under 10 us, buffered throughput over 1e6 int elements/s
  at capacity 1024, and under 64 KiB per idle channel (the ring is 4 KiB).
* **scaling:** 1/2/4-pair aggregate throughput is recorded for context and is
  *not* gated; the host is shared.
* **crug/pool:** the ratio is recorded, never gated.  The pool arm submits
  preallocated jobs and allocates no stack per task, while every Crug task
  reserves and commits its own stack, so the ratio is ~12-14x by construction.

## Human output

    concurrency: dispatch pthread=<ns> pool=<ns> crug=<ns> ratio=<crug/pthread> pool-ratio=<crug/pool> ... tasks=200 lost=0
    concurrency: memory-per-task pthread=<KiB> crug=<KiB>
    channels: latency=<ns>/roundtrip throughput=<elem/s> memory=<B> scaling=<a>/<b>/<c> elem/s
    concurrency: wrote <path/to/results.json>
    concurrency: OK|FAIL

Failed checks are listed between the channels line and the final line
(the wrote line only appears when `--json` is given).

## JSON

`--json PATH` writes one object (`schema: 1`, `phase: 21`):

    {
      "schema": 1, "phase": 21, "area": "concurrency+channels", "ok": true,
      "host":   {"machine", "cpus", "loadavg", "cc", "workers"},
      "config": {"dispatch_tasks", "rounds", "throughput_elements",
                 "throughput_capacity", "scale_elements", "scale_pairs",
                 "memory_channels", "memory_capacity",
                 "shots": {"dispatch", "memory", "channels"},
                 "bands": {...}},
      "build":  {"cc", "flags", "objects"},
      "dispatch": {
        "tasks": 200,
        "arms": {"pthread|pool|pool-warm|crug": {"ns", "ns_per_task",
                                                 "checksums", "lost", "runs"}},
        "ratio_crug_pthread": <float>,
        "ratio_crug_pool": <float>,
        "pool_ratio_target": 1.5, "pool_ratio_met": <bool>
      },
      "memory": {
        "tasks": 200,
        "arms": {"pthread|crug": {"vmsize_kb", "rss_kb", "bytes_per_task",
                                  "kib_per_task", "tasks", "checksum", "runs"}},
        "ratio_crug_pthread": <float>
      },
      "channels": {
        "pingpong":   {"best_ns", "rounds", "ns_per_roundtrip", "checksums", "runs"},
        "throughput": {"best_ns", "elements", "capacity", "elem_per_s",
                       "checksums", "runs"},
        "memory":     {"per_channel_bytes", "ring_bytes", "overhead_bytes",
                       "rss_per_channel_bytes", "channels", "capacity",
                       "elem_size", "checksum", "runs"},
        "scaling":    {"1|2|4": {"pairs", "best_ns", "elements_per_pair",
                                 "aggregate_elem_per_s", "checksums", "runs"}},
        "scaling_ratios": {"2": <float>, "4": <float>}
      },
      "checks": [{"name", "ok", "value", "limit", "detail"}, ...]
    }

Timings are nanoseconds, memory is bytes (and KiB where the name says so),
throughput is elements/second.  `runs` is the number of process-level shots
that contributed to a minimum.

## Methodology notes

* Dispatch and channel timings exclude runtime start-up and shutdown, except
  for the pthread and pool arms whose creation is the measured work; gates
  and atomic counters keep the timing window to the actual task bodies.
* The Crug memory arm spawns one `prewarm_task` and waits for it before
  sampling the baseline, so the runtime's worker threads are counted as fixed
  overhead rather than as per-task cost.  On glibc the program pins
  `M_ARENA_MAX` to 1: per-thread malloc arenas reserve 64 MiB of PROT_NONE
  address space each, which otherwise appears and disappears in VmSize and
  swamps a 68 KiB per-task delta (samples that shrink are retried, never
  recorded).
* `chanbench memory` creates 2048 channels of capacity 1024 and divides the
  VmSize delta, after checking every allocation is accounted for.
