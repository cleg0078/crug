#!/bin/sh
# check-strict-suite.sh - run the Crug strict/raw/unsafe dg suite and report its tally.
#
# Runs "make check-STRICT_UNSAFE", which bootstraps DejaGnu if needed and
# executes gcc/testsuite/crug.dg/strict/strict.exp, then reads the summary file.
#
# Usage: scripts/check-strict-suite.sh
# Exit: 0 all strict tests pass; 1 any test fails or the suite did not run; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=$ROOT/build/check-strict-suite.log
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum

mkdir -p "$ROOT/build"

if ! make -C "$ROOT" check-STRICT_UNSAFE >"$LOG" 2>&1; then
	echo "strict-suite: make check-STRICT_UNSAFE failed; see $LOG" >&2
	exit 1
fi

if [ ! -f "$SUM" ]; then
	echo "strict-suite: no summary file at $SUM" >&2
	exit 1
fi

pass=$(grep -c '^PASS: crug.dg/strict/' "$SUM")
fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/strict/' "$SUM")
total=$((pass + fail))

if [ "$total" -eq 0 ]; then
	echo "strict-suite: no crug.dg/strict tests ran; see $LOG" >&2
	exit 1
fi

echo "strict-suite: $total tests, $fail failures"

if [ "$fail" -ne 0 ]; then
	grep -E '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/strict/' "$SUM" >&2
	exit 1
fi

exit 0
