#!/bin/sh
# check-strict-raw.sh - a raw dereference is accepted in hybrid mode and
# rejected with its stable code under -fcrug-strict.
#
# The same fixture is compiled twice: without the strict flag it must exit 0,
# with the flag it must exit 1 and print [crug-SAFE-001].
#
# Usage: scripts/check-strict-raw.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 both observations hold; 1 one failed; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC=$ROOT/gcc/testsuite/crug.dg/strict/raw-deref.c
OUT=$ROOT/build/strict-raw

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "strict-raw: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

"$CRUGC" -O2 -fcrug-ownership -fsyntax-only "$SRC" >"$OUT/hybrid.log" 2>&1
hybrid=$?
"$CRUGC" -O2 -fcrug-strict -fsyntax-only "$SRC" >"$OUT/strict.log" 2>&1
strict=$?

if [ "$hybrid" -ne 0 ]; then
	echo "strict-raw: hybrid mode rejected the file (exit $hybrid)" >&2
	head -3 "$OUT/hybrid.log" >&2
	exit 1
fi

if [ "$strict" -ne 1 ] || ! grep -q '\[crug-SAFE-001\]' "$OUT/strict.log"; then
	echo "strict-raw: strict mode did not report [crug-SAFE-001] (exit $strict)" >&2
	head -3 "$OUT/strict.log" >&2
	exit 1
fi

echo "strict-raw: hybrid exit 0, strict [crug-SAFE-001]"
exit 0
