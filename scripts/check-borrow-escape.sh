#!/bin/sh
# check-borrow-escape.sh - a borrow cannot leave the function that made it.
#
# The rejection has to be at the use site (the return) and carry the origin of
# the borrow in a note, because the reader needs to see both the escape and the
# object that would be left behind.
#
# Usage: scripts/check-borrow-escape.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 rejected at the use site with the origin noted; 1 otherwise; 2 cannot
#       run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-escape
LOG=$OUT/escape-return.log

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-escape: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

if "$CRUGC" -O2 -fcrug-borrow -Wno-crug-unadopted-own -fsyntax-only \
	"$DIR/escape-return.c" >"$LOG" 2>&1; then
	echo "FAIL: escape-return.c compiled but returns a borrow" >&2
	exit 1
fi

bad=0
if ! grep -q 'crug-BORROW-019' "$LOG"; then
	echo "FAIL: no crug-BORROW-019 in the diagnostics" >&2
	bad=1
fi
if ! grep -q 'note: borrow created here' "$LOG"; then
	echo "FAIL: the origin of the borrow is not noted" >&2
	bad=1
fi
if [ "$bad" -ne 0 ]; then
	head -5 "$LOG" >&2
	exit 1
fi

echo "borrow-escape: return rejected at use site, origin noted"
exit 0
