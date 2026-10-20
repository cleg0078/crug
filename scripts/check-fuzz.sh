#!/bin/sh
# check-fuzz.sh - Phase 22 fuzz gate: run all six harnesses and adjudicate.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The compiler harnesses (parser, ownership, borrow, .crugffi) live in
# fuzz/compiler and the runtime harnesses (scheduler, channel state machine)
# live in fuzz/runtime.  Each script owns its own corpus and budget; this
# script only runs both, refuses to call a run clean unless both report their
# harness count and OK line, and counts findings written during this run.
#
# Budget: FUZZ_BUDGET compiler inputs per target (default 300) and FUZZ_RUNS
# runtime sequences/runs (default from fuzz/runtime).  The nightly workflow
# raises FUZZ_BUDGET to 2000.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2

BUDGET=${FUZZ_BUDGET:-300}
LOGDIR="$ROOT/build/fuzz"
FINDINGS=${FUZZ_FINDINGS:-$LOGDIR/findings}
mkdir -p "$LOGDIR" "$FINDINGS" || exit 2
MARKER="$LOGDIR/check-FUZZ.marker"
: > "$MARKER"

say () { printf 'fuzz: %s\n' "$*"; }
die () { printf 'fuzz: %s\n' "$*" >&2; exit 2; }

compiler=$ROOT/fuzz/compiler/fuzz.py
runtime=$ROOT/fuzz/runtime/run.sh
[ -f "$compiler" ] || die "missing $compiler"
[ -f "$runtime" ] || die "missing $runtime"

say "budget: $BUDGET compiler input(s) per target, runtime budget from FUZZ_RUNS"
rc=0
python3 "$compiler" --check --budget "$BUDGET" 2>&1 | tee "$LOGDIR/compiler.log" || rc=1
sh "$runtime" 2>&1 | tee "$LOGDIR/runtime.log" || rc=1

grep -q 'compiler-fuzz: 4 harness(es)' "$LOGDIR/compiler.log" || { say 'the compiler run did not report four harnesses'; rc=1; }
grep -q 'compiler-fuzz: OK' "$LOGDIR/compiler.log" || rc=1
grep -q 'runtime-fuzz: 2 harness(es)' "$LOGDIR/runtime.log" || { say 'the runtime run did not report two harnesses'; rc=1; }
grep -q 'runtime-fuzz: OK' "$LOGDIR/runtime.log" || rc=1

new=$(find "$FINDINGS" -type f -newer "$MARKER" 2>/dev/null | wc -l | tr -d ' ')
[ "$new" -eq 0 ] || { say "$new new finding(s) under $FINDINGS"; rc=1; }

if [ "$rc" -ne 0 ]; then
  say 'FAIL: a harness reported a crash, a timeout or a new finding'
  say "logs: $LOGDIR/compiler.log $LOGDIR/runtime.log"
  exit 1
fi

say '6 harness(es), 0 crash(es), 0 timeout(s), 0 new finding(s)'
say 'OK'
exit 0