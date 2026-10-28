#!/usr/bin/env python3
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
"""Timing harness for the c2crug candidate-pair cost.

Generates a corpus with N candidate pairs (each pair is one malloc/free
declaration in a function body) and times one tool on it.  The default
corpus puts every pair in the same function, which is the shape that
exposes per-candidate rescans: a tool that re-tokenises or re-parses the
whole body for every candidate scales quadratically in N.

Usage:
  python3 migration/bench-c2crug.py --pairs 100 200 400 800
  python3 migration/bench-c2crug.py --baseline build/c2perf/c2crug-pristine
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "build", "c2crug-bench")

TEMPLATE_HEAD = """#include <stdlib.h>

int
run (void)
{
"""

TEMPLATE_PAIR = """  int *p%d = malloc (sizeof (int));
  p%d[0] = %d;
  free (p%d);
"""

TEMPLATE_TAIL = """  return 0;
}
"""


def corpus(root, pairs):
    path = os.path.join(root, "bench.c")
    with open(path, "w", encoding="utf-8") as f:
        f.write(TEMPLATE_HEAD)
        for index in range(pairs):
            f.write(TEMPLATE_PAIR % (index, index, index, index))
        f.write(TEMPLATE_TAIL)
    return path


def time_tool(tool, root, runs):
    report = os.path.join(root, "report.json")
    best = None
    for _ in range(runs):
        start = time.perf_counter()
        proc = subprocess.run([sys.executable, tool, "--analyze", root,
                               "--report", report],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True)
        elapsed = time.perf_counter() - start
        if proc.returncode != 0:
            raise SystemExit("tool failed: %s" % proc.stderr.strip())
        best = elapsed if best is None else min(best, elapsed)
    return best


def main(argv):
    ap = argparse.ArgumentParser(description="time c2crug on N candidate pairs")
    ap.add_argument("--tool", default=os.path.join(ROOT, "c2crug"))
    ap.add_argument("--baseline", default=None,
                    help="second tool to time (for example the pristine copy)")
    ap.add_argument("--pairs", type=int, nargs="+", default=[100, 200, 400, 800])
    ap.add_argument("--runs", type=int, default=3)
    args = ap.parse_args(argv)

    os.makedirs(WORK, exist_ok=True)
    tools = [("tool", args.tool)]
    if args.baseline:
        tools.append(("baseline", args.baseline))
    print("%6s %12s %12s %8s" % ("pairs", "tool (s)", "baseline (s)", "speedup"))
    for pairs in args.pairs:
        root = os.path.join(WORK, "n%d" % pairs)
        shutil.rmtree(root, ignore_errors=True)
        os.makedirs(root)
        corpus(root, pairs)
        results = {}
        for label, tool in tools:
            results[label] = time_tool(tool, root, args.runs)
        speedup = ""
        if "baseline" in results:
            speedup = "%.2fx" % (results["baseline"] / results["tool"])
        print("%6d %12.3f %12s %8s"
              % (pairs, results["tool"],
                 "%.3f" % results["baseline"] if "baseline" in results else "-",
                 speedup))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
