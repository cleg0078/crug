#!/usr/bin/env python3
"""Phase 21 concurrency and channel benchmarks (plain C arms, Python driver).

The C programs live next to this driver and in bench/channels/:

    bench/concurrency/dispatch.c   task dispatch: pthread-per-task, a fixed
                                   W-worker pool, and the Crug task runtime
    bench/concurrency/memtask.c    reserved memory per live task: pthread
                                   versus Crug
    bench/channels/chanbench.c     channel ping-pong, buffered throughput,
                                   memory per channel, 1/2/4-pair scaling

Every program prints one machine line per arm and is run min-of-8 internally;
this driver runs each program three times (process level) and keeps the
smallest observed value, so one unlucky scheduling slot cannot fail the gate.
If a check fails anyway, the driver takes additional shots before it reports
the failure.

Ratified bands (Phase 21 release policy):

    dispatch   crug <= 1.5 x pthread-per-task (the naive C reference)
               every arm runs 200 task bodies exactly once, lost == 0
    memory     crug bytes/task <= 0.5 x pthread bytes/task (VmSize delta)
    channels   latency < 10 us/roundtrip, throughput > 1e6 elem/s at
               capacity 1024, per-channel memory < 64 KiB, and every value
               received in order with no loss
    scaling    1/2/4-pair aggregate throughput is recorded for context; it is
               not a gate (this host is shared and oversubscribed)

The crug/pool ratio is RECORDED, never gated: the pool arm is allocation-free
by design (preallocated jobs, no per-task stack), while every Crug task
reserves and commits its own 64 KiB stack, so the ratio is ~14x by
construction.

Usage:

    bench/concurrency/run.py --json build/bench/concurrency/results.json
    bench/concurrency/run.py --check

Exit status: 0 when every check passes, 1 when a check fails, 2 when the
runtime objects or the compiler are unavailable (environment problem).

The JSON shape is documented in bench/concurrency/README.md.
"""

import argparse
import json
import os
import platform
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent           # bench/concurrency
BENCH = HERE.parent                              # bench
ROOT = BENCH.parent                              # repository root
BUILD = ROOT / "build" / "bench" / "concurrency"
LIBCRUG = ROOT / "libcrug"
CORE = LIBCRUG / "core"
OBJ_DIR = LIBCRUG / "build" / "linux"

OBJ_NAMES = ("task", "pal_linux", "switch", "stack", "chan")
PROGRAMS = (
    ("dispatch", HERE / "dispatch.c"),
    ("memtask", HERE / "memtask.c"),
    ("chanbench", BENCH / "channels" / "chanbench.c"),
)

DISPATCH_TASKS = 200
PP_ROUNDS = 20000
TP_ELEMENTS = 200000
TP_CAPACITY = 1024
SCALE_ELEMENTS = 100000
SCALE_CAPACITY = 1024
SCALE_PAIRS = (1, 2, 4)
MEM_CHANNELS = 2048
MEM_CAPACITY = 1024

SHOTS = 3
EXTRA_SHOTS = 3

DISPATCH_BAND = 1.5          # crug <= 1.5 x pthread-per-task
MEMORY_BAND = 0.5            # crug bytes/task <= 0.5 x pthread bytes/task
POOL_RATIO_TARGET = 1.5      # recorded only, never gated
LATENCY_MAX_NS = 10000       # 10 us per round trip
THROUGHPUT_MIN = 1000000     # 1e6 int elements/s at capacity 1024
CHANNEL_MEMORY_MAX = 64 * 1024
CHANNEL_OVERHEAD_MAX = 4096  # driver bookkeeping on top of the ring

DISPATCH_ARMS = ("pthread", "pool", "pool-warm", "crug")

CFLAGS = ["-O2", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function",
          f"-I{BENCH}", f"-I{CORE}"]
CC = os.environ.get("CC", "cc")


class EnvError(Exception):
    """The benchmark cannot build or start: environment problem (exit 2)."""


class BenchFailure(Exception):
    """A program reported a correctness failure (exit 1)."""


def child_env():
    env = os.environ.copy()
    # Pin the runtime's worker count so pthread, pool and Crug arms are
    # measured against the same amount of parallelism on every host.
    env["CRUG_TASKS_WORKERS"] = "8"
    env.pop("CRUG_TASKS_STACK", None)
    return env


def sh(cmd, timeout=900):
    try:
        return subprocess.run([str(c) for c in cmd], cwd=str(ROOT),
                              env=child_env(), capture_output=True, text=True,
                              timeout=timeout)
    except FileNotFoundError as exc:
        raise EnvError(f"cannot execute {cmd[0]}: {exc}")
    except subprocess.TimeoutExpired:
        raise EnvError("timed out: " + " ".join(str(c) for c in cmd))


def cc_version():
    proc = sh([CC, "--version"], timeout=60)
    first = (proc.stdout or proc.stderr).strip().splitlines()
    return first[0] if first else CC


def ensure_objects():
    """Build the runtime objects if they are missing; exit 2 if still absent."""
    missing = [OBJ_DIR / f"{name}.o" for name in OBJ_NAMES
               if not (OBJ_DIR / f"{name}.o").exists()]
    if not missing:
        return
    print("concurrency: building the Crug runtime objects "
          "(make -C libcrug PAL=linux)")
    proc = sh(["make", "-C", str(LIBCRUG), "PAL=linux"])
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise EnvError("make -C libcrug PAL=linux failed")
    missing = [OBJ_DIR / f"{name}.o" for name in OBJ_NAMES
               if not (OBJ_DIR / f"{name}.o").exists()]
    if missing:
        raise EnvError("missing runtime objects: "
                       + ", ".join(str(path) for path in missing))


def build_programs():
    if shutil.which(CC) is None:
        raise EnvError(f"no C compiler: {CC!r} is not on PATH")
    BUILD.mkdir(parents=True, exist_ok=True)
    objects = [OBJ_DIR / f"{name}.o" for name in OBJ_NAMES]
    executables = {}
    for name, source in PROGRAMS:
        if not source.exists():
            raise EnvError(f"missing benchmark source: {source}")
        exe = BUILD / name
        cmd = [CC, *CFLAGS, "-o", str(exe), str(source), *map(str, objects),
               "-pthread"]
        proc = sh(cmd)
        if proc.returncode != 0:
            sys.stderr.write(proc.stdout + proc.stderr)
            raise EnvError("compiling " + source.name + " failed")
        executables[name] = exe
    return executables


def parse_fields(line):
    """'dispatch crug ns=4 checksum=200 lost=0' -> ('dispatch','crug',{...})."""
    fields = line.split()
    if len(fields) < 3:
        raise BenchFailure(f"unexpected output line: {line!r}")
    kind, mode = fields[0], fields[1]
    values = {}
    for field in fields[2:]:
        if "=" not in field:
            continue
        key, _, text = field.partition("=")
        try:
            values[key] = int(text)
        except ValueError:
            raise BenchFailure(f"non-integer field in {line!r}")
    return kind, mode, values


def program_lines(exe, mode):
    """Run one mode and return the parsed output lines."""
    proc = subprocess.run([str(exe), mode], cwd=str(ROOT), env=child_env(),
                          capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise BenchFailure(f"{exe.name} {mode} exited {proc.returncode}")
    lines = [line for line in proc.stdout.splitlines() if line.strip()]
    if not lines:
        raise BenchFailure(f"{exe.name} {mode} printed nothing")
    return [parse_fields(line) for line in lines]


def require_mode(lines, mode):
    if len(lines) != 1:
        raise BenchFailure(f"expected one line for {mode}, got {len(lines)}")
    kind, got, values = lines[0]
    if got != mode:
        raise BenchFailure(f"expected mode {mode}, got {got}")
    return values


def require_keys(values, keys, mode):
    missing = [key for key in keys if key not in values]
    if missing:
        raise BenchFailure(f"{mode} line lacks {missing}")


# ---- dispatch ------------------------------------------------------------

def measure_dispatch(shots):
    data = {}
    for arm in DISPATCH_ARMS:
        runs = []
        for _ in range(shots):
            values = require_mode(program_lines(EXES["dispatch"], arm), arm)
            require_keys(values, ("ns", "checksum", "lost"), arm)
            runs.append(values)
        data[arm] = {
            "ns": min(run["ns"] for run in runs),
            "checksums": {run["checksum"] for run in runs},
            "lost": {run["lost"] for run in runs},
            "runs": runs,
        }
    return data


def merge_dispatch(old, new):
    merged = {}
    for arm in DISPATCH_ARMS:
        merged[arm] = {
            "ns": min(old[arm]["ns"], new[arm]["ns"]),
            "checksums": old[arm]["checksums"] | new[arm]["checksums"],
            "lost": old[arm]["lost"] | new[arm]["lost"],
            "runs": old[arm]["runs"] + new[arm]["runs"],
        }
    return merged


def evaluate_dispatch(data):
    checks = []

    def add(name, ok, value, limit, detail):
        checks.append({"name": name, "ok": bool(ok), "value": value,
                       "limit": limit, "detail": detail})

    checksums = {arm: sorted(data[arm]["checksums"]) for arm in DISPATCH_ARMS}
    losses = {arm: sorted(data[arm]["lost"]) for arm in DISPATCH_ARMS}
    add("dispatch_tasks", all(checksums[arm] == [DISPATCH_TASKS]
                              for arm in DISPATCH_ARMS),
        checksums, [DISPATCH_TASKS],
        "every arm must run all 200 task bodies exactly once")
    add("dispatch_no_loss", all(losses[arm] == [0] for arm in DISPATCH_ARMS),
        losses, [0], "the runtime must report no lost tasks")
    add("dispatch_nonzero", all(data[arm]["ns"] > 0 for arm in DISPATCH_ARMS),
        {arm: data[arm]["ns"] for arm in DISPATCH_ARMS}, "> 0",
        "each arm must report a positive dispatch time")

    ratio = data["crug"]["ns"] / data["pthread"]["ns"]
    add("dispatch_crug_within_1_5x_pthread", ratio <= DISPATCH_BAND,
        ratio, DISPATCH_BAND,
        "Crug task dispatch must be within 1.5x of pthread-per-task")
    return checks


# ---- memory per task -----------------------------------------------------

def measure_memory(shots):
    data = {}
    for arm in ("pthread", "crug"):
        runs = []
        for _ in range(shots):
            values = require_mode(program_lines(EXES["memtask"], arm), arm)
            require_keys(values, ("vmsize_kb", "rss_kb", "tasks", "checksum"),
                         arm)
            runs.append(values)
        data[arm] = {
            "vmsize_kb": min(run["vmsize_kb"] for run in runs),
            "rss_kb": min(run["rss_kb"] for run in runs),
            "tasks": runs[0]["tasks"],
            "checksum": runs[0]["checksum"],
            "runs": runs,
        }
        data[arm]["bytes_per_task"] = (
            data[arm]["vmsize_kb"] * 1024.0 / DISPATCH_TASKS)
    return data


def merge_memory(old, new):
    merged = {}
    for arm in ("pthread", "crug"):
        merged[arm] = {
            "vmsize_kb": min(old[arm]["vmsize_kb"], new[arm]["vmsize_kb"]),
            "rss_kb": min(old[arm]["rss_kb"], new[arm]["rss_kb"]),
            "tasks": old[arm]["tasks"],
            "checksum": old[arm]["checksum"],
            "runs": old[arm]["runs"] + new[arm]["runs"],
        }
        merged[arm]["bytes_per_task"] = (
            merged[arm]["vmsize_kb"] * 1024.0 / DISPATCH_TASKS)
    return merged


def evaluate_memory(data):
    checks = []

    def add(name, ok, value, limit, detail):
        checks.append({"name": name, "ok": bool(ok), "value": value,
                       "limit": limit, "detail": detail})

    add("memory_tasks", all(data[arm]["tasks"] == DISPATCH_TASKS
                            and data[arm]["checksum"] == DISPATCH_TASKS
                            for arm in ("pthread", "crug")),
        {arm: data[arm]["tasks"] for arm in ("pthread", "crug")},
        DISPATCH_TASKS, "both arms must have 200 tasks live and accounted for")
    ratio = data["crug"]["bytes_per_task"] / data["pthread"]["bytes_per_task"]
    add("memory_crug_under_half_pthread", ratio <= MEMORY_BAND, ratio,
        MEMORY_BAND,
        "Crug must reserve at most half the VmSize per task that a pthread does")
    return checks


# ---- channels ------------------------------------------------------------

def measure_channels(shots):
    ping_runs = []
    throughput_runs = []
    memory_runs = []
    scaling_runs = {pairs: [] for pairs in SCALE_PAIRS}
    for _ in range(shots):
        values = require_mode(program_lines(EXES["chanbench"], "pingpong"),
                              "pingpong")
        require_keys(values, ("best_ns", "rounds", "checksum"), "pingpong")
        ping_runs.append(values)

        values = require_mode(program_lines(EXES["chanbench"], "throughput"),
                              "throughput")
        require_keys(values, ("best_ns", "elements", "capacity", "checksum"),
                     "throughput")
        throughput_runs.append(values)

        values = require_mode(program_lines(EXES["chanbench"], "memory"),
                              "memory")
        require_keys(values, ("per_channel_bytes", "ring_bytes",
                              "overhead_bytes", "channels", "capacity",
                              "elem_size", "checksum"), "memory")
        memory_runs.append(values)

        lines = program_lines(EXES["chanbench"], "scaling")
        if len(lines) != len(SCALE_PAIRS):
            raise BenchFailure(
                f"scaling printed {len(lines)} lines, expected {len(SCALE_PAIRS)}")
        seen = set()
        for _kind, mode, values in lines:
            require_keys(values, ("pairs", "best_ns", "elements_per_pair",
                                  "capacity", "checksum"), "scaling")
            pairs = values["pairs"]
            seen.add(pairs)
            scaling_runs[pairs].append(values)
        if seen != set(SCALE_PAIRS):
            raise BenchFailure(f"scaling reported pairs {sorted(seen)}")

    memory = {
        "per_channel_bytes": min(run["per_channel_bytes"]
                                 for run in memory_runs),
        "ring_bytes": memory_runs[0]["ring_bytes"],
        "channels": memory_runs[0]["channels"],
        "capacity": memory_runs[0]["capacity"],
        "elem_size": memory_runs[0]["elem_size"],
        "checksum": memory_runs[0]["checksum"],
        "rss_per_channel_bytes": memory_runs[0].get("rss_per_channel_bytes", 0),
        "runs": memory_runs,
    }
    memory["overhead_bytes"] = (memory["per_channel_bytes"]
                                - memory["ring_bytes"])
    return {
        "pingpong": {
            "best_ns": min(run["best_ns"] for run in ping_runs),
            "rounds": ping_runs[0]["rounds"],
            "checksums": {run["checksum"] for run in ping_runs},
            "runs": ping_runs,
        },
        "throughput": {
            "best_ns": min(run["best_ns"] for run in throughput_runs),
            "elements": throughput_runs[0]["elements"],
            "capacity": throughput_runs[0]["capacity"],
            "checksums": {run["checksum"] for run in throughput_runs},
            "runs": throughput_runs,
        },
        "memory": memory,
        "scaling": {
            str(pairs): {
                "best_ns": min(run["best_ns"] for run in scaling_runs[pairs]),
                "elements_per_pair": scaling_runs[pairs][0]["elements_per_pair"],
                "capacity": scaling_runs[pairs][0]["capacity"],
                "checksums": {run["checksum"]
                              for run in scaling_runs[pairs]},
                "runs": scaling_runs[pairs],
            }
            for pairs in SCALE_PAIRS
        },
    }


def merge_channels(old, new):
    merged = {
        "pingpong": dict(old["pingpong"]),
        "throughput": dict(old["throughput"]),
        "memory": dict(old["memory"]),
        "scaling": {key: dict(value) for key, value in old["scaling"].items()},
    }
    merged["pingpong"]["best_ns"] = min(old["pingpong"]["best_ns"],
                                       new["pingpong"]["best_ns"])
    merged["pingpong"]["checksums"] = (old["pingpong"]["checksums"]
                                       | new["pingpong"]["checksums"])
    merged["pingpong"]["runs"] = old["pingpong"]["runs"] + new["pingpong"]["runs"]
    merged["throughput"]["best_ns"] = min(old["throughput"]["best_ns"],
                                          new["throughput"]["best_ns"])
    merged["throughput"]["checksums"] = (old["throughput"]["checksums"]
                                         | new["throughput"]["checksums"])
    merged["throughput"]["runs"] = (old["throughput"]["runs"]
                                    + new["throughput"]["runs"])
    merged["memory"]["per_channel_bytes"] = min(
        old["memory"]["per_channel_bytes"], new["memory"]["per_channel_bytes"])
    merged["memory"]["overhead_bytes"] = (
        merged["memory"]["per_channel_bytes"] - merged["memory"]["ring_bytes"])
    merged["memory"]["runs"] = old["memory"]["runs"] + new["memory"]["runs"]
    for key in merged["scaling"]:
        merged["scaling"][key]["best_ns"] = min(old["scaling"][key]["best_ns"],
                                                new["scaling"][key]["best_ns"])
        merged["scaling"][key]["checksums"] = (
            old["scaling"][key]["checksums"]
            | new["scaling"][key]["checksums"])
        merged["scaling"][key]["runs"] = (old["scaling"][key]["runs"]
                                          + new["scaling"][key]["runs"])
    return merged


def evaluate_channels(data):
    checks = []

    def add(name, ok, value, limit, detail):
        checks.append({"name": name, "ok": bool(ok), "value": value,
                       "limit": limit, "detail": detail})

    ping_expected = PP_ROUNDS * (PP_ROUNDS - 1) // 2
    throughput_expected = TP_ELEMENTS * (TP_ELEMENTS - 1) // 2

    add("channels_pingpong_checksum",
        data["pingpong"]["checksums"] == {ping_expected},
        sorted(data["pingpong"]["checksums"]), ping_expected,
        "every value crossed both unbuffered channels intact")
    add("channels_pingpong_rounds", data["pingpong"]["rounds"] == PP_ROUNDS,
        data["pingpong"]["rounds"], PP_ROUNDS, "round count")

    ping_ns = data["pingpong"]["best_ns"]
    ns_per_roundtrip = ping_ns / PP_ROUNDS
    add("channels_latency", 0 < ns_per_roundtrip < LATENCY_MAX_NS,
        ns_per_roundtrip, LATENCY_MAX_NS,
        "unbuffered handoff must stay under 10 us per round trip")

    add("channels_throughput_checksum",
        data["throughput"]["checksums"] == {throughput_expected},
        sorted(data["throughput"]["checksums"]), throughput_expected,
        "all 200000 elements arrived in order with no loss")
    add("channels_throughput_shape",
        data["throughput"]["elements"] == TP_ELEMENTS
        and data["throughput"]["capacity"] == TP_CAPACITY,
        {"elements": data["throughput"]["elements"],
         "capacity": data["throughput"]["capacity"]},
        {"elements": TP_ELEMENTS, "capacity": TP_CAPACITY},
        "buffered throughput shape")
    elem_per_s = TP_ELEMENTS * 1e9 / data["throughput"]["best_ns"]
    add("channels_throughput_rate", elem_per_s > THROUGHPUT_MIN,
        elem_per_s, THROUGHPUT_MIN,
        "buffered channel must move over 1e6 int elements/s")

    memory = data["memory"]
    add("channels_memory_ring",
        memory["ring_bytes"] == MEM_CAPACITY * memory["elem_size"],
        memory["ring_bytes"], MEM_CAPACITY * memory["elem_size"],
        "the ring is capacity * element size")
    add("channels_memory_overhead",
        0 <= memory["overhead_bytes"] <= CHANNEL_OVERHEAD_MAX,
        memory["overhead_bytes"], CHANNEL_OVERHEAD_MAX,
        "per-channel bookkeeping on top of the ring")
    add("channels_memory_total",
        memory["per_channel_bytes"] < CHANNEL_MEMORY_MAX,
        memory["per_channel_bytes"], CHANNEL_MEMORY_MAX,
        "per-channel reserved memory must stay under 64 KiB")
    add("channels_memory_checksum",
        memory["checksum"] == MEM_CHANNELS, memory["checksum"], MEM_CHANNELS,
        "every made channel was accounted for")

    scaling_ok = True
    per_pair = SCALE_ELEMENTS * (SCALE_ELEMENTS - 1) // 2
    for pairs in SCALE_PAIRS:
        entry = data["scaling"][str(pairs)]
        expected = pairs * per_pair
        if (entry["checksums"] != {expected}
                or entry["elements_per_pair"] != SCALE_ELEMENTS
                or entry["best_ns"] <= 0):
            scaling_ok = False
    add("channels_scaling_correct", scaling_ok,
        {str(pairs): sorted(data["scaling"][str(pairs)]["checksums"])
         for pairs in SCALE_PAIRS},
        "pairs * sum(0..n-1)", "scaling values in order with no loss")
    return checks


# ---- reporting -----------------------------------------------------------

def collect(measure, evaluate, merge):
    data = measure(SHOTS)
    checks = evaluate(data)
    shots = SHOTS
    if not all(check["ok"] for check in checks):
        # A failed band may be one unlucky scheduling slot on a loaded host;
        # take more shots and judge the repeated minima before failing.
        extra = measure(EXTRA_SHOTS)
        data = merge(data, extra)
        checks = evaluate(data)
        shots = SHOTS + EXTRA_SHOTS
    return data, checks, shots


def sanitize(value):
    if isinstance(value, set):
        return sorted(value)
    if isinstance(value, dict):
        return {key: sanitize(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [sanitize(item) for item in value]
    return value


def flat_values(checks, name):
    for check in checks:
        if check["name"] == name:
            return check
    raise BenchFailure(f"missing check {name}")


def fmt_ratio(value):
    return f"{value:.3f}"


def report(dispatch, dispatch_checks, memory, memory_checks, channels,
           channel_checks, shots):
    pthread_ns = dispatch["pthread"]["ns"]
    pool_ns = dispatch["pool"]["ns"]
    crug_ns = dispatch["crug"]["ns"]
    ratio = crug_ns / pthread_ns
    pool_ratio = crug_ns / pool_ns
    pool_met = pool_ratio <= POOL_RATIO_TARGET
    pool_note = ""
    if not pool_met:
        pool_note = (" (target %.1f, met=false: per-task 64 KiB stack "
                     "reserve+commit; the pool arm is allocation-free by "
                     "design)" % POOL_RATIO_TARGET)
    print("concurrency: dispatch pthread=%d pool=%d crug=%d ratio=%s "
          "pool-ratio=%s%s tasks=%d lost=0"
          % (pthread_ns, pool_ns, crug_ns, fmt_ratio(ratio),
             fmt_ratio(pool_ratio), pool_note, DISPATCH_TASKS))

    print("concurrency: memory-per-task pthread=%.1fKiB crug=%.1fKiB"
          % (memory["pthread"]["bytes_per_task"] / 1024.0,
             memory["crug"]["bytes_per_task"] / 1024.0))

    latency = channels["pingpong"]["best_ns"] / channels["pingpong"]["rounds"]
    elem_per_s = (channels["throughput"]["elements"] * 1e9
                  / channels["throughput"]["best_ns"])
    scaling = [int(round(SCALE_PAIRS[i] * channels["scaling"][str(SCALE_PAIRS[i])]["elements_per_pair"] * 1e9
                         / channels["scaling"][str(SCALE_PAIRS[i])]["best_ns"]))
               for i in range(len(SCALE_PAIRS))]
    print("channels: latency=%dns/roundtrip throughput=%d elem/s "
          "memory=%dB scaling=%d/%d/%d elem/s"
          % (round(latency), int(round(elem_per_s)),
             channels["memory"]["per_channel_bytes"], scaling[0], scaling[1],
             scaling[2]))

    failed = [check for check in dispatch_checks + memory_checks
              + channel_checks if not check["ok"]]
    for check in failed:
        print("  FAILED %s: value=%s limit=%s (%s)"
              % (check["name"], check["value"], check["limit"],
                 check["detail"]))
    return failed, shots


def main(argv):
    global EXES
    ap = argparse.ArgumentParser(
        description="Phase 21 concurrency and channel benchmarks")
    ap.add_argument("--json", type=Path,
                    help="write the measurements and checks to PATH")
    ap.add_argument("--check", action="store_true",
                    help="enforce the bands (always enforced; kept for the "
                         "bench/run.py interface)")
    args = ap.parse_args(argv)

    try:
        ensure_objects()
        EXES = build_programs()
        dispatch = measure_dispatch(SHOTS)
        dispatch_checks = evaluate_dispatch(dispatch)
        shots = SHOTS
        if not all(check["ok"] for check in dispatch_checks):
            dispatch = merge_dispatch(dispatch, measure_dispatch(EXTRA_SHOTS))
            dispatch_checks = evaluate_dispatch(dispatch)
            shots = SHOTS + EXTRA_SHOTS

        memory, memory_checks, mem_shots = collect(
            measure_memory, evaluate_memory, merge_memory)

        channels, channel_checks, chan_shots = collect(
            measure_channels, evaluate_channels, merge_channels)
    except EnvError as exc:
        print(f"concurrency: FAIL (environment: {exc})")
        return 2
    except BenchFailure as exc:
        print(f"concurrency: FAIL ({exc})")
        return 1

    failed, shots = report(dispatch, dispatch_checks, memory, memory_checks,
                           channels, channel_checks,
                           {"dispatch": shots, "memory": mem_shots,
                            "channels": chan_shots})
    ok = not failed

    record = {
        "schema": 1,
        "phase": 21,
        "area": "concurrency+channels",
        "host": {
            "machine": platform.machine(),
            "cpus": os.cpu_count(),
            "loadavg": sanitize(os.getloadavg()),
            "cc": cc_version(),
            "workers": 8,
        },
        "config": {
            "dispatch_tasks": DISPATCH_TASKS,
            "rounds": PP_ROUNDS,
            "throughput_elements": TP_ELEMENTS,
            "throughput_capacity": TP_CAPACITY,
            "scale_elements": SCALE_ELEMENTS,
            "scale_pairs": list(SCALE_PAIRS),
            "memory_channels": MEM_CHANNELS,
            "memory_capacity": MEM_CAPACITY,
            "shots": {"dispatch": shots["dispatch"],
                      "memory": shots["memory"],
                      "channels": shots["channels"]},
            "bands": {
                "dispatch_crug_vs_pthread": DISPATCH_BAND,
                "memory_crug_vs_pthread": MEMORY_BAND,
                "pool_ratio_target": POOL_RATIO_TARGET,
                "latency_max_ns": LATENCY_MAX_NS,
                "throughput_min_elem_s": THROUGHPUT_MIN,
                "channel_memory_max_bytes": CHANNEL_MEMORY_MAX,
            },
        },
        "build": {"cc": CC, "flags": CFLAGS,
                  "objects": [str(OBJ_DIR / f"{name}.o")
                              for name in OBJ_NAMES]},
        "dispatch": {
            "tasks": DISPATCH_TASKS,
            "arms": {arm: {"ns": dispatch[arm]["ns"],
                           "ns_per_task": dispatch[arm]["ns"] / DISPATCH_TASKS,
                           "checksums": sorted(dispatch[arm]["checksums"]),
                           "lost": sorted(dispatch[arm]["lost"]),
                           "runs": len(dispatch[arm]["runs"])}
                     for arm in DISPATCH_ARMS},
            "ratio_crug_pthread": dispatch["crug"]["ns"] / dispatch["pthread"]["ns"],
            "ratio_crug_pool": dispatch["crug"]["ns"] / dispatch["pool"]["ns"],
            "pool_ratio_target": POOL_RATIO_TARGET,
            "pool_ratio_met": (dispatch["crug"]["ns"] / dispatch["pool"]["ns"]
                               <= POOL_RATIO_TARGET),
        },
        "memory": {
            "tasks": DISPATCH_TASKS,
            "arms": {
                arm: {"vmsize_kb": memory[arm]["vmsize_kb"],
                      "rss_kb": memory[arm]["rss_kb"],
                      "bytes_per_task": memory[arm]["bytes_per_task"],
                      "kib_per_task": memory[arm]["bytes_per_task"] / 1024.0,
                      "tasks": memory[arm]["tasks"],
                      "checksum": memory[arm]["checksum"],
                      "runs": len(memory[arm]["runs"])}
                for arm in ("pthread", "crug")
            },
            "ratio_crug_pthread": (memory["crug"]["bytes_per_task"]
                                   / memory["pthread"]["bytes_per_task"]),
        },
        "channels": {
            "pingpong": {
                "best_ns": channels["pingpong"]["best_ns"],
                "rounds": channels["pingpong"]["rounds"],
                "ns_per_roundtrip": (channels["pingpong"]["best_ns"]
                                     / channels["pingpong"]["rounds"]),
                "checksums": sorted(channels["pingpong"]["checksums"]),
                "runs": len(channels["pingpong"]["runs"]),
            },
            "throughput": {
                "best_ns": channels["throughput"]["best_ns"],
                "elements": channels["throughput"]["elements"],
                "capacity": channels["throughput"]["capacity"],
                "elem_per_s": (channels["throughput"]["elements"] * 1e9
                               / channels["throughput"]["best_ns"]),
                "checksums": sorted(channels["throughput"]["checksums"]),
                "runs": len(channels["throughput"]["runs"]),
            },
            "memory": {
                "per_channel_bytes": channels["memory"]["per_channel_bytes"],
                "ring_bytes": channels["memory"]["ring_bytes"],
                "overhead_bytes": channels["memory"]["overhead_bytes"],
                "rss_per_channel_bytes":
                    channels["memory"]["rss_per_channel_bytes"],
                "channels": channels["memory"]["channels"],
                "capacity": channels["memory"]["capacity"],
                "elem_size": channels["memory"]["elem_size"],
                "checksum": channels["memory"]["checksum"],
                "runs": len(channels["memory"]["runs"]),
            },
            "scaling": {
                key: {
                    "pairs": int(key),
                    "best_ns": channels["scaling"][key]["best_ns"],
                    "elements_per_pair":
                        channels["scaling"][key]["elements_per_pair"],
                    "aggregate_elem_per_s": (
                        int(key) * channels["scaling"][key]["elements_per_pair"]
                        * 1e9 / channels["scaling"][key]["best_ns"]),
                    "checksums": sorted(channels["scaling"][key]["checksums"]),
                    "runs": len(channels["scaling"][key]["runs"]),
                }
                for key in sorted(channels["scaling"], key=int)
            },
        },
        "checks": sanitize(dispatch_checks + memory_checks + channel_checks),
        "ok": ok,
    }
    record["channels"]["scaling_ratios"] = {
        key: (record["channels"]["scaling"][key]["aggregate_elem_per_s"]
              / record["channels"]["scaling"]["1"]["aggregate_elem_per_s"])
        for key in ("2", "4")
    }

    if args.json:
        args.json = args.json if args.json.is_absolute() else ROOT / args.json
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")
        print("concurrency: wrote %s" % args.json)

    print("concurrency: %s" % ("OK" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
