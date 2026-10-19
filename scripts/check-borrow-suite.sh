#!/bin/sh
# check-borrow-suite.sh - run the Crug borrow dg suite and report its tally.
#
# Runs "make check-BORROW", which bootstraps DejaGnu if needed and executes
# gcc/testsuite/crug.dg/borrow/borrow.exp, then reads the summary file.
#
# Usage: scripts/check-borrow-suite.sh
# Exit: 0 all borrow tests pass; 1 any test fails or the suite did not run; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=$ROOT/build/check-borrow-suite.log
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum

mkdir -p "$ROOT/build"

if ! make -C "$ROOT" check-BORROW >"$LOG" 2>&1; then
	echo "borrow-suite: make check-BORROW failed; see $LOG" >&2
	exit 1
fi

if [ ! -f "$SUM" ]; then
	echo "borrow-suite: no summary file at $SUM" >&2
	exit 1
fi

pass=$(grep -c '^PASS: crug.dg/borrow/' "$SUM")
fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/borrow/' "$SUM")
total=$((pass + fail))

if [ "$total" -eq 0 ]; then
	echo "borrow-suite: no crug.dg/borrow tests ran; see $LOG" >&2
	exit 1
fi

echo "borrow-suite: $total tests, $fail failures"

if [ "$fail" -ne 0 ]; then
	grep -E '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/borrow/' "$SUM" >&2
	exit 1
fi

exit 0
