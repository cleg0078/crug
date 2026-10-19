#!/bin/sh
# check-borrow-policy.sh - every borrow rejection carries its stable code.
#
# Compiles one fixture per rejection class and requires the documented code in
# the diagnostics: a mutable borrow while shared (BORROW-002), a second mutable
# borrow (BORROW-003), a move while borrowed (BORROW-004), a drop while
# borrowed (BORROW-005), a borrow that outlives its lender (BORROW-007), and a
# write through a shared borrow (BORROW-008).
#
# Usage: scripts/check-borrow-policy.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 every code was reported; 1 one was missing; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-policy

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-policy: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

bad=0
summary=

check ()
{
	fixture=$1
	code=$2
	if ! "$CRUGC" -O2 -fcrug-borrow -Wno-crug-unadopted-own -fsyntax-only \
		"$DIR/$fixture" >"$OUT/$fixture.log" 2>&1; then
		if grep -q "crug-$code" "$OUT/$fixture.log"; then
			summary="$summary $code"
		else
			printf 'FAIL %s: crug-%s missing\n' "$fixture" "$code" >&2
			head -3 "$OUT/$fixture.log" >&2
			bad=$((bad + 1))
		fi
	else
		printf 'FAIL %s: compiled but should be rejected\n' "$fixture" >&2
		bad=$((bad + 1))
	fi
}

check r01-mut-plus-ref.c BORROW-002
check r02-mut-twice.c BORROW-003
check r04-move-while-borrowed.c BORROW-004
check r05-drop-while-borrowed.c BORROW-005
check r18-field-borrow.c BORROW-007
check r07-write-through-ref.c BORROW-008

echo "borrow-policy:$summary"

if [ "$bad" -ne 0 ]; then
	exit 1
fi

exit 0
