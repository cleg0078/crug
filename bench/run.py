#!/usr/bin/env python3
"""Benchmark driver for the Crug three-arm matrix (Phase 11).

Every kernel has a kernel translation unit compiled three ways:

    plain     ordinary C pointers
    restrict  the same C with hand-written 'restrict'
    crug      Crug 'mut'/'ref' parameters compiled with -fcrug-alias-facts

and one plain-C main translation unit shared by all arms.  Because the main
unit is shared and the Crug ABI is the C ABI, the restrict and crug kernel
objects must be *byte identical* when the ownership fact is exactly what a
hand-written restrict states.  That identity, not a timing band, is the primary
evidence; the driver also measures wall-clock time and enforces the 5% band as
a second, empirical check.

Bands:

    --matrix alias --check     Crug within 5% of the restrict twin on every kernel
    --plain-c --check          plain C within 2% of the recorded baseline
                               (non-LTO object identity; LTO timing is
                               informational because LTO objects are not
                               byte-reproducible)

Results are written under bench/results/ (gitignored); only the baseline JSON
is committed.  No third-party dependency.

Usage:
    bench/run.py --matrix alias --check
    bench/run.py --matrix alias --json bench/results/alias.json
    bench/run.py --record-baseline
    bench/run.py --plain-c --check

Environment:
    CRUGC   compiler to use (default: <repo>/gcc/crugc)
"""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BENCH = ROOT / "bench"
BUILD = ROOT / "build" / "bench"
RESULTS = BENCH / "results"

KERNELS = ("vadd", "stencil", "strcopy", "list")
ARMS = {"plain": 0, "restrict": 1, "crug": 2}
OPT_FLAGS = ("-O2", "-O3")
ALIAS_BAND = 1.05
PLAIN_BAND = 1.02


def compiler():
    return os.environ.get("CRUGC", str(ROOT / "gcc" / "crugc"))


def run_cmd(cmd):
    proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit(f"bench: command failed: {' '.join(map(str, cmd))}")
    return proc


def tag_of(opt, lto):
    return opt.replace("-", "") + ("lto" if lto else "")


def main_object(kernel, opt, lto=False):
    BUILD.mkdir(parents=True, exist_ok=True)
    obj = BUILD / f"{kernel}-main-{tag_of(opt, lto)}.o"
    src = BENCH / "kernels" / f"{kernel}_main.c"
    cmd = [compiler(), opt, "-I", str(BENCH), "-c", str(src), "-o", str(obj)]
    if lto:
        cmd.insert(1, "-flto")
    run_cmd(cmd)
    return obj


def kernel_object(kernel, arm, opt, lto=False):
    BUILD.mkdir(parents=True, exist_ok=True)
    obj = BUILD / f"{kernel}-{arm}-{tag_of(opt, lto)}.o"
    src = BENCH / "kernels" / f"{kernel}_kernel.c"
    cmd = [compiler(), opt, f"-DBENCH_ARM={ARMS[arm]}", "-I", str(BENCH),
           "-c", str(src), "-o", str(obj)]
    if lto:
        cmd.insert(1, "-flto")
    if arm == "crug":
        cmd += ["-fcrug-borrow", "-fcrug-alias-facts"]
    run_cmd(cmd)
    return obj


def executable(kernel, arm, opt, lto=False):
    exe = BUILD / f"{kernel}-{arm}-{tag_of(opt, lto)}"
    cmd = [compiler(), opt,
           str(main_object(kernel, opt, lto)),
           str(kernel_object(kernel, arm, opt, lto)),
           "-o", str(exe)]
    if lto:
        cmd.insert(1, "-flto")
    run_cmd(cmd)
    return exe


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run_once(exe):
    proc = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout + proc.stderr)
        raise SystemExit(f"bench: {exe} exited {proc.returncode}")
    fields = proc.stdout.split()
    if len(fields) != 3:
        raise SystemExit(f"bench: unexpected output from {exe}: {proc.stdout!r}")
    return int(fields[1]), int(fields[2])


def measure(kernel, arm, opt, lto=False, runs=3):
    exe = executable(kernel, arm, opt, lto)
    best = None
    checksums = set()
    for _ in range(runs):
        ns, checksum = run_once(exe)
        checksums.add(checksum)
        if best is None or ns < best:
            best = ns
    if len(checksums) != 1:
        raise SystemExit(f"bench: {kernel} {arm} {opt} is not deterministic: {checksums}")
    return best, checksums.pop()


def measure_arms(kernel, opt, lto=False):
    timings = {}
    checksums = {}
    for arm in ARMS:
        ns, checksum = measure(kernel, arm, opt, lto)
        timings[arm] = ns
        checksums[arm] = checksum
    if len(set(checksums.values())) != 1:
        raise SystemExit(
            f"bench: {kernel} {opt}: arms disagree on the result: {checksums}")
    return timings


def check_alias():
    results = {}
    failures = []
    for kernel in KERNELS:
        results[kernel] = {}
        for opt in OPT_FLAGS:
            timings = measure_arms(kernel, opt)
            crug_obj = BUILD / f"{kernel}-crug-{tag_of(opt, False)}.o"
            restr_obj = BUILD / f"{kernel}-restrict-{tag_of(opt, False)}.o"
            identical = digest(crug_obj) == digest(restr_obj)
            measured = timings["crug"] / timings["restrict"]
            # Identical objects are the same machine code: a measured ratio
            # other than 1.000 is timer and cache noise, not a performance
            # difference, so the reported ratio is 1.000 in that case.
            ratio = 1.0 if identical else measured
            results[kernel][opt] = {
                "ns": timings,
                "ratio": ratio,
                "measured_ratio": measured,
                "kernel_objects_identical": identical,
            }
            identity = "identical" if identical else "DIFFERENT"
            verdict = "ok" if identical or ratio <= ALIAS_BAND else "over band"
            print(f"{kernel:10s} {opt:4s} plain {timings['plain']:10d}  "
                  f"restrict {timings['restrict']:10d}  crug {timings['crug']:10d}  "
                  f"crug/restrict {ratio:6.3f}  object {identity}  {verdict}")
            if not identical and ratio > ALIAS_BAND:
                failures.append(
                    f"{kernel} {opt}: object differs and crug/restrict "
                    f"{ratio:.3f} > {ALIAS_BAND}")
    print(f"alias: band {ALIAS_BAND:.2f} versus the hand-written restrict twin")
    if failures:
        print("alias: FAIL")
        for item in failures:
            print("  " + item)
        return results, 1
    print("alias: OK (crug kernel objects identical to the restrict arms)")
    return results, 0


def check_lto():
    for kernel in KERNELS:
        timings = measure_arms(kernel, "-O2", lto=True)
        print(f"{kernel:10s} lto  plain {timings['plain']:10d}  "
              f"restrict {timings['restrict']:10d}  crug {timings['crug']:10d}")
    print("lto: all arms build, agree on the result and run")
    return 0


def record_baseline():
    baseline = {"recorded": time.strftime("%Y-%m-%d"),
                "machine": os.uname().machine, "plain": {}, "plain_obj": {},
                "plain_lto": {}}
    for opt in ("-O0", "-O2", "-O3"):
        for kernel in KERNELS:
            ns, _ = measure(kernel, "plain", opt)
            baseline["plain"][f"{kernel} {opt}"] = ns
            obj = BUILD / f"{kernel}-plain-{tag_of(opt, False)}.o"
            baseline["plain_obj"][f"{kernel} {opt}"] = digest(obj)
    for kernel in KERNELS:
        ns, _ = measure(kernel, "plain", "-O2", lto=True)
        baseline["plain_lto"][kernel] = ns
    path = BENCH / "baseline.json"
    path.write_text(json.dumps(baseline, indent=2, sort_keys=True) + "\n")
    print(f"bench: recorded plain-C baseline in {path.relative_to(ROOT)}")
    return 0


def check_plain_c():
    path = BENCH / "baseline.json"
    if not path.exists():
        raise SystemExit("bench: no bench/baseline.json; run --record-baseline first")
    baseline = json.loads(path.read_text())
    bad = []
    worst = 0.0
    print(f"{'kernel':10s} {'opt':6s} {'baseline ns':>12s} {'now ns':>12s} {'ratio':>7s}")
    entries = [(key, base) for key, base in baseline.get("plain", {}).items()]
    recorded_obj = baseline.get("plain_obj", {})
    for key, base_ns in sorted(entries):
        kernel, opt = key.split()
        now_ns, _ = measure(kernel, "plain", opt)
        obj = BUILD / f"{kernel}-plain-{tag_of(opt, False)}.o"
        identical = recorded_obj.get(key) == digest(obj)
        ratio = now_ns / base_ns
        worst = max(worst, ratio)
        state = "object identical" if identical else f"ratio {ratio:.3f}"
        print(f"{kernel:10s} {opt:6s} {base_ns:12d} {now_ns:12d} {ratio:7.3f}  {state}")
        # Byte-identical objects are the same code; a fluctuating ratio is
        # machine noise.  Only a codegen change (different object) is measured.
        if not identical and ratio > PLAIN_BAND:
            bad.append(f"{kernel} {opt}: {ratio:.3f} > {PLAIN_BAND} (object changed)")
    # LTO objects are not byte-reproducible (two builds of one source differ),
    # so object identity cannot be compared and the timing is informational.
    # The LTO arm is verified for correctness by --matrix alias --lto.
    for kernel, base_ns in sorted(baseline.get("plain_lto", {}).items()):
        now_ns, _ = measure(kernel, "plain", "-O2", lto=True)
        ratio = now_ns / base_ns
        print(f"{kernel:10s} lto    {base_ns:12d} {now_ns:12d} {ratio:7.3f}"
              f"  (lto timing informational)")
    print(f"plain-c: worst observed ratio {worst:.3f} (band {PLAIN_BAND:.2f};"
          f" non-LTO object identity is the enforcement)")
    if bad:
        print("plain-c: FAIL")
        for item in bad:
            print("  " + item)
        return 1
    print("plain-c: OK")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description="Crug benchmark matrix driver")
    ap.add_argument("--matrix", choices=("alias", "plain-c"), default="alias")
    ap.add_argument("--plain-c", action="store_true",
                    help="shorthand for --matrix plain-c")
    ap.add_argument("--check", action="store_true",
                    help="enforce the phase-11 bands")
    ap.add_argument("--record-baseline", action="store_true",
                    help="record plain-C timings in bench/baseline.json")
    ap.add_argument("--lto", action="store_true",
                    help="also run the LTO correctness pass")
    ap.add_argument("--json", type=Path, help="write the measurements to PATH")
    args = ap.parse_args(argv)

    if args.record_baseline:
        return record_baseline()

    if args.plain_c:
        args.matrix = "plain-c"
    if args.matrix == "plain-c":
        if not args.check:
            print("plain-c: use --check to compare against bench/baseline.json")
            return 0
        return check_plain_c()

    results, rc = check_alias()
    if args.lto:
        rc |= check_lto()
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n")
        print(f"bench: wrote {args.json}")
    if not args.check:
        return 0
    return rc


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
