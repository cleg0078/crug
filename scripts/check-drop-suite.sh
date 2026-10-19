#!/bin/sh
# check-drop-suite.sh - run the Crug drop dg suite and report its tally.
#
# Runs "make check-DROP", which bootstraps DejaGnu if needed and executes
# gcc/testsuite/crug.dg/drop/drop.exp, then reads the summary file.
#
# Usage: scripts/check-drop-suite.sh
# Exit: 0 all drop tests pass; 1 any test fails or the suite did not run; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=$ROOT/build/check-drop-suite.log
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum

mkdir -p "$ROOT/build"

if ! make -C "$ROOT" check-DROP >"$LOG" 2>&1; then
	echo "drop-suite: make check-DROP failed; see $LOG" >&2
	exit 1
fi

if [ ! -f "$SUM" ]; then
	echo "drop-suite: no summary file at $SUM" >&2
	exit 1
fi

pass=$(grep -c '^PASS: crug.dg/drop/' "$SUM")
fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/drop/' "$SUM")
total=$((pass + fail))

if [ "$total" -eq 0 ]; then
	echo "drop-suite: no crug.dg/drop tests ran; see $LOG" >&2
	exit 1
fi

echo "drop-suite: $total tests, $fail failures"

if [ "$fail" -ne 0 ]; then
	grep -E '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/drop/' "$SUM" >&2
	exit 1
fi

exit 0
