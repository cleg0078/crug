#!/bin/sh
# check-safe-types.sh - run the Crug safe core type suite and report its tally.
#
# Runs "make check-SAFE_TYPES", which bootstraps DejaGnu if needed and executes
# gcc/testsuite/crug.dg/safe-types/safe-types.exp, then reads the summary file.
#
# Usage: scripts/check-safe-types.sh
# Exit: 0 all safe-type tests pass; 1 any test fails or the suite did not run;
#       2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LOG=$ROOT/build/check-safe-types-suite.log
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum

mkdir -p "$ROOT/build"

if ! make -C "$ROOT" check-SAFE_TYPES >"$LOG" 2>&1; then
	echo "safe-types: make check-SAFE_TYPES failed; see $LOG" >&2
	exit 1
fi

if [ ! -f "$SUM" ]; then
	echo "safe-types: no summary file at $SUM" >&2
	exit 1
fi

pass=$(grep -c '^PASS: crug.dg/safe-types/' "$SUM")
fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/safe-types/' "$SUM")
total=$((pass + fail))

if [ "$total" -eq 0 ]; then
	echo "safe-types: no crug.dg/safe-types tests ran; see $LOG" >&2
	exit 1
fi

echo "safe-types: $total tests, $fail failures"

if [ "$fail" -ne 0 ]; then
	grep -E '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/safe-types/' "$SUM" >&2
	exit 1
fi

exit 0
