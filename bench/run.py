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

Phase 21 adds four qualification modes:

    --zero-cost      optimised IR and assembly of a Crug construct versus its
                     plain-C twin (own/drop, move, ref, mut)
    --pristine       object parity against an unmodified GCC 16.2 compiler
                     built in build/pristine-obj (make pristine-xgcc)
    --concurrency    run bench/concurrency/run.py (tasks and channels)
    --kernel         run scripts/check-kernel-perf.sh (kernel corpus codegen)
    --performance    run all of the above and write bench/results/<date>.md

Usage:
    bench/run.py --matrix alias --check
    bench/run.py --matrix alias --json bench/results/alias.json
    bench/run.py --record-baseline
    bench/run.py --plain-c --check
    bench/run.py --zero-cost --check
    bench/run.py --pristine --check
    bench/run.py --concurrency --check
    bench/run.py --kernel --check
    bench/run.py --performance --check

Environment:
    CRUGC   compiler to use (default: <repo>/gcc/crugc)
"""

import argparse
import contextlib
import hashlib
import io
import json
import os
import re
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

# Phase 21 zero-cost matrix: each case is a Crug construct and a plain-C twin
# with the same semantics.  Both are compiled from the same basename so that
# the assembly and the tree dump line up.  The optimised tree dump is compared
# rather than the initial GIMPLE, because the initial dump differs cosmetically
# (one extra empty label for an automatic drop, different temporary numbers).
ZC_DIR = BENCH / "zero-cost"
ZC_CASES = (
    ("own-drop", ("-fcrug-ownership",)),
    ("move", ("-fcrug-ownership",)),
    ("ref-param", ("-fcrug-borrow",)),
    ("mut-param", ("-fcrug-borrow",)),
)
ZC_LEVELS = ("-O0", "-O2", "-O3")
PRISTINE_XGCC = ROOT / "build" / "pristine-obj" / "gcc" / "xgcc"


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


def pristine_xgcc():
    return PRISTINE_XGCC


def compile_with(cc, flags, src, obj, include_bench=True):
    obj.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(cc)] + list(flags)
    if include_bench:
        cmd += ["-I", str(BENCH)]
    cmd += ["-c", str(src), "-o", str(obj)]
    run_cmd(cmd)
    return obj


def zc_compile(case, twin, flags, opt):
    """Compile one side of a zero-cost case to assembly plus the optimised dump."""
    src = ZC_DIR / case / (twin + ".c")
    if not src.exists():
        raise SystemExit(f"bench: missing zero-cost source {src}")
    work = BUILD / "zero-cost" / case / opt.lstrip("-") / twin
    work.mkdir(parents=True, exist_ok=True)
    target = work / "case.c"
    target.write_text(src.read_text())
    asm = work / "case.s"
    cmd = [compiler(), opt, *flags, "-fno-diagnostics-color",
           "-fdump-tree-optimized", "-S", str(target), "-o", str(asm)]
    run_cmd(cmd)
    dumps = sorted(work.glob("case.c.*.optimized"))
    if len(dumps) != 1:
        raise SystemExit(
            f"bench: expected one optimised dump in {work}, found {len(dumps)}")
    return asm, dumps[0]


def asm_instructions(path):
    """Instruction lines of an assembly file, directives and comments removed."""
    out = []
    for raw in path.read_text().splitlines():
        if not raw.startswith("\t"):
            continue
        body = raw.strip()
        if not body or body.startswith("."):
            continue
        body = body.split("#")[0].strip()
        if body:
            out.append(body)
    return out


def normalize_asm(lines):
    """Rename local labels and strings by first use so two compilations compare.

    GCC numbers local symbols per translation unit.  The numbering carries no
    performance meaning, so both sides are canonicalised to .L0, .L1, ... in
    order of first appearance.
    """
    mapping = {}

    def repl(match):
        symbol = match.group(0)
        if symbol not in mapping:
            mapping[symbol] = ".L%d" % len(mapping)
        return mapping[symbol]

    return [re.sub(r"\.LC?[0-9]+", repl, line) for line in lines]


def normalize_tree(text):
    """Blank the temporary numbers so dumps from two compilations compare.

    At -O0 the optimised dump still carries declared temporaries; their
    numbering says nothing about the work performed.
    """
    return re.sub(r"D\.[0-9]+", "D.TMP", text)


def call_targets(lines):
    out = []
    for line in lines:
        match = re.match(r"callq?\s+(\S+)$", line)
        if match:
            out.append(match.group(1))
    return out


def check_zero_cost(check=True, json_path=None):
    records = {}
    failures = []
    comparisons = 0
    added_instructions = 0
    added_calls = 0
    for case, flags in ZC_CASES:
        records[case] = {}
        for opt in ZC_LEVELS:
            plain_asm, plain_dump = zc_compile(case, "plain", (), opt)
            crug_asm, crug_dump = zc_compile(case, "crug", flags, opt)
            plain_lines = normalize_asm(asm_instructions(plain_asm))
            crug_lines = normalize_asm(asm_instructions(crug_asm))
            plain_calls = call_targets(plain_lines)
            crug_calls = call_targets(crug_lines)
            ir_identical = (normalize_tree(plain_dump.read_text())
                            == normalize_tree(crug_dump.read_text()))
            asm_identical = plain_lines == crug_lines
            record = {
                "optimised_ir_identical": ir_identical,
                "assembly_identical": asm_identical,
                "instructions": {"plain": len(plain_lines), "crug": len(crug_lines)},
                "calls": {"plain": len(plain_calls), "crug": len(crug_calls)},
                "added_instructions": max(0, len(crug_lines) - len(plain_lines)),
                "added_calls": max(0, len(crug_calls) - len(plain_calls)),
                "ok": ir_identical and asm_identical,
            }
            records[case][opt] = record
            comparisons += 1
            added_instructions += record["added_instructions"]
            added_calls += record["added_calls"]
            print("zero-cost: %-9s %-3s instructions %d = %d, calls %d = %d, "
                  "IR %s => %s"
                  % (case, opt, len(crug_lines), len(plain_lines),
                     len(crug_calls), len(plain_calls),
                     "identical" if ir_identical else "DIFFERS",
                     "ok" if record["ok"] else "DIFFERS"))
            if not record["ok"]:
                failures.append("%s %s: assembly or optimised IR differs"
                                % (case, opt))
    print("zero-cost: %d comparison(s), %d added instruction(s), %d added call(s)"
          % (comparisons, added_instructions, added_calls))
    if json_path:
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")
        print("bench: wrote %s" % json_path)
    if failures:
        print("zero-cost: FAIL")
        for item in failures:
            print("  " + item)
        return 1
    if check:
        print("zero-cost: OK (every Crug construct lowers exactly to its plain-C twin)")
    return 0


def check_pristine(check=True, json_path=None):
    """Compare codegen with an unmodified GCC 16.2 build (make pristine-xgcc)."""
    pristine = PRISTINE_XGCC
    if not pristine.exists():
        print("pristine: SKIP (no unmodified compiler at %s; run make pristine-xgcc)"
              % pristine.relative_to(ROOT))
        return 2
    base = ["-B" + str(pristine.parent) + "/"]
    sources = sorted((ROOT / "corpus").glob("*.c"))
    sources += sorted((BENCH / "kernels").glob("*.c"))
    if not sources:
        raise SystemExit("bench: no plain-C sources for the pristine comparison")
    bad = []
    pairs = 0
    records = {"sources": {}, "lto": {}}
    for opt in ZC_LEVELS:
        for src in sources:
            tag = "%s-%s" % (opt.lstrip("-"), src.stem)
            ref = BUILD / "pristine" / "reference" / (tag + ".o")
            cur = BUILD / "pristine" / "crug" / (tag + ".o")
            compile_with(pristine, base + [opt], src, ref)
            compile_with(compiler(), [opt], src, cur)
            pairs += 1
            identical = digest(ref) == digest(cur)
            records["sources"]["%s %s" % (src.name, opt)] = identical
            if not identical:
                bad.append("%s %s" % (src.relative_to(ROOT), opt))
    # LTO objects are not byte-reproducible, so the executables are compared
    # first; only when those differ is the timing band applied.
    lto_bad = []
    for kernel in KERNELS:
        main_src = BENCH / "kernels" / (kernel + "_main.c")
        kern_src = BENCH / "kernels" / (kernel + "_kernel.c")
        exes = {}
        for side, cc, flags in (("reference", pristine, base), ("crug", compiler(), ())):
            exe = BUILD / "pristine" / ("%s-%s-lto" % (kernel, side))
            cmd = [str(cc)] + list(flags) + ["-O2", "-flto", "-I", str(BENCH),
                   str(main_src), str(kern_src), "-o", str(exe)]
            run_cmd(cmd)
            exes[side] = exe
        identical = digest(exes["reference"]) == digest(exes["crug"])
        records["lto"][kernel] = {"identical": identical}
        if not identical:
            ref_ns, _ = run_once(exes["reference"])
            crug_ns, _ = run_once(exes["crug"])
            ratio = crug_ns / ref_ns
            records["lto"][kernel]["ratio"] = ratio
            print("pristine: %s lto executable differs, ratio %.3f" % (kernel, ratio))
            if ratio > PLAIN_BAND:
                lto_bad.append("%s lto: %.3f > %.2f" % (kernel, ratio, PLAIN_BAND))
    print("pristine: %d source/option pair(s), %d object difference(s), "
          "%d LTO timing violation(s) against the unmodified GCC 16.2"
          % (pairs, len(bad), len(lto_bad)))
    for item in bad:
        print("  object differs: " + item)
    for item in lto_bad:
        print("  " + item)
    if json_path:
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n")
        print("bench: wrote %s" % json_path)
    if bad or lto_bad:
        print("pristine: FAIL")
        return 1
    if check:
        print("pristine: OK (plain-C codegen is byte-identical to GCC 16.2)")
    return 0


def check_concurrency(check=True):
    script = BENCH / "concurrency" / "run.py"
    if not script.exists():
        print("concurrency: SKIP (bench/concurrency/run.py is absent)")
        return 2
    cmd = [sys.executable, str(script)]
    if check:
        cmd.append("--check")
    proc = subprocess.run(cmd, cwd=str(ROOT), capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


def check_kernel(check=True):
    script = ROOT / "scripts" / "check-kernel-perf.sh"
    if not script.exists():
        print("kernel: SKIP (scripts/check-kernel-perf.sh is absent)")
        return 2
    proc = subprocess.run([str(script)], cwd=str(ROOT), capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    sys.stderr.write(proc.stderr)
    return proc.returncode


def capture(fn, *args, **kwargs):
    """Run a checker while keeping its output for the report and the console."""
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        rc = fn(*args, **kwargs)
    text = buffer.getvalue()
    sys.stdout.write(text)
    return rc, text.rstrip("\n").splitlines()


def write_performance_report(date, sections, rc):
    RESULTS.mkdir(parents=True, exist_ok=True)
    path = RESULTS / (date + ".md")
    fence = chr(96) * 3
    host = " ".join((os.uname().sysname, os.uname().machine))
    lines = [
        "# Crug performance qualification (" + date + ")",
        "",
        "- host: " + host + ", " + str(os.cpu_count()) + " CPUs",
        "- compiler under test: " + str(compiler()),
        "- pristine compiler: " + str(PRISTINE_XGCC),
        "- driver: bench/run.py --performance --check",
        "",
    ]
    for title, crc, body in sections:
        lines.append("## " + title)
        lines.append("")
        lines.append("Result: " + ("pass" if crc == 0 else "FAIL (exit " + str(crc) + ")"))
        lines.append("")
        lines.append(fence)
        lines.extend(body)
        lines.append(fence)
        lines.append("")
    lines.append("## Verdict")
    lines.append("")
    lines.append("pass" if rc == 0 else "FAIL")
    lines.append("")
    path.write_text("\n".join(lines))
    print("performance: report written to " + str(path.relative_to(ROOT)))
    return path


def run_performance():
    date = time.strftime("%Y-%m-%d")
    sections = []
    rc = 0
    for title, fn in (
            ("Zero cost", check_zero_cost),
            ("Pristine compiler parity", check_pristine),
            ("Recorded plain-C baseline", check_plain_c),
            ("Concurrency and channels", check_concurrency),
            ("Kernel codegen", check_kernel)):
        crc, body = capture(fn)
        sections.append((title, crc, body))
        if crc != 0:
            rc = 1
    write_performance_report(date, sections, rc)
    print("performance: " + ("OK" if rc == 0 else "FAIL"))
    return rc


def main(argv):
    ap = argparse.ArgumentParser(description="Crug benchmark driver")
    ap.add_argument("--matrix", choices=("alias", "plain-c"), default="alias")
    ap.add_argument("--plain-c", action="store_true",
                    help="shorthand for --matrix plain-c")
    ap.add_argument("--check", action="store_true",
                    help="enforce the qualification bands")
    ap.add_argument("--record-baseline", action="store_true",
                    help="record plain-C timings in bench/baseline.json")
    ap.add_argument("--lto", action="store_true",
                    help="also run the LTO correctness pass")
    ap.add_argument("--json", type=Path, help="write the measurements to PATH")
    ap.add_argument("--zero-cost", action="store_true",
                    help="compare each Crug construct with its plain-C twin")
    ap.add_argument("--pristine", action="store_true",
                    help="compare plain-C codegen with an unmodified GCC 16.2")
    ap.add_argument("--concurrency", action="store_true",
                    help="run the task and channel benchmarks")
    ap.add_argument("--kernel", action="store_true",
                    help="run the kernel codegen comparison")
    ap.add_argument("--performance", action="store_true",
                    help="run every Phase 21 qualification and write the report")
    args = ap.parse_args(argv)

    if args.record_baseline:
        return record_baseline()
    if args.zero_cost:
        return check_zero_cost(check=args.check, json_path=args.json)
    if args.pristine:
        return check_pristine(check=args.check, json_path=args.json)
    if args.concurrency:
        return check_concurrency(check=args.check)
    if args.kernel:
        return check_kernel(check=args.check)
    if args.performance:
        return run_performance()

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
