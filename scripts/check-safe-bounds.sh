#!/bin/sh
# check-safe-bounds.sh - slice indexing checks in range, traps out of range,
# and leaves plain C indexing alone.
#
# Compiles three programs from gcc/testsuite/crug.dg/safe-types/bounds/ and
# asserts four properties:
#   1. an in-range program exits 0 in checked mode;
#   2. an out-of-range program dies on SIGILL in checked mode, because the
#      comparison lowers to BUILT_IN_TRAP;
#   3. the checked object contains a trap instruction and the
#      -fno-crug-bounds-checks object contains none;
#   4. an ordinary C array subscript never gains a trap, even with
#      -fcrug-slices (MASTER_SPEC section 19.4: no checks on C pointers).
#
# Usage: scripts/check-safe-bounds.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 all four properties hold; 1 one of them fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/safe-types/bounds
OUT=$ROOT/build/safe-bounds

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "safe-bounds: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

bad=0

compile () {
	# compile <source> <flags> <object>
	if ! "$CRUGC" -O2 $2 -c "$DIR/$1" -o "$3" >"$OUT/$1.log" 2>&1; then
		printf 'FAIL %s: does not compile with %s\n' "$1" "$2"
		head -3 "$OUT/$1.log"
		bad=$((bad + 1))
		return 1
	fi
	return 0
}

if compile in-range.c "-fcrug-slices" "$OUT/in-range.o"; then
	if ! "$CRUGC" -O2 -fcrug-slices "$DIR/in-range.c" -o "$OUT/in-range" >"$OUT/in-range.link.log" 2>&1; then
		printf 'FAIL in-range: does not link\n'
		bad=$((bad + 1))
	elif ! "$OUT/in-range" >"$OUT/in-range.out" 2>&1; then
		printf 'FAIL in-range: exits non-zero in checked mode\n'
		bad=$((bad + 1))
	fi
fi

if compile oob.c "-fcrug-slices" "$OUT/oob-checked.o"; then
	traps=$(objdump -d "$OUT/oob-checked.o" | grep -c 'ud2')
	if [ "$traps" -eq 0 ]; then
		printf 'FAIL oob-checked: no trap instruction in the checked object\n'
		bad=$((bad + 1))
	fi
	if ! "$CRUGC" -O2 -fcrug-slices "$DIR/oob.c" -o "$OUT/oob-checked" >"$OUT/oob.link.log" 2>&1; then
		printf 'FAIL oob-checked: does not link\n'
		bad=$((bad + 1))
	else
		"$OUT/oob-checked" >"$OUT/oob.out" 2>&1
		rc=$?
		if [ "$rc" -ne 132 ]; then
			printf 'FAIL oob-checked: expected SIGILL (132), got exit %s\n' "$rc"
			bad=$((bad + 1))
		fi
	fi
fi

if compile oob.c "-fcrug-slices -fno-crug-bounds-checks" "$OUT/oob-unchecked.o"; then
	traps=$(objdump -d "$OUT/oob-unchecked.o" | grep -c 'ud2')
	if [ "$traps" -ne 0 ]; then
		printf 'FAIL oob-unchecked: still has %s trap instructions\n' "$traps"
		bad=$((bad + 1))
	fi
fi

if compile plain-array.c "-fcrug-slices" "$OUT/plain-array.o"; then
	traps=$(objdump -d "$OUT/plain-array.o" | grep -c 'ud2')
	if [ "$traps" -ne 0 ]; then
		printf 'FAIL plain-array: a C subscript gained %s trap instructions\n' "$traps"
		bad=$((bad + 1))
	fi
fi

if [ "$bad" -ne 0 ]; then
	echo "safe-bounds: $bad properties failed"
	exit 1
fi

echo "safe-bounds: in-range 0, out-of-range SIGILL, unchecked no trap, plain C no trap"
exit 0
