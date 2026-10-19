#!/bin/sh
# check-borrow-cost.sh - a borrow is a plain pointer at code generation time.
#
# Compiles the same two function bodies twice: once with ref/mut borrows and
# borrow()/borrow_mut(), once with ordinary pointers.  The objects must be
# byte-identical and must contain no Crug helper call.  A borrow is a static
# fact, so nothing is emitted for it (ADR 0003 D1, RFC-0001 section 18).
#
# Both sources are copied to the same file name in different directories so
# that the object file's STT_FILE symbol and debug strings match too.
#
# Usage: scripts/check-borrow-cost.sh
# Environment: CRUGC overrides the driver path; OBJDUMP, NM override binutils.
# Exit: 0 identical objects and no helper; 1 a difference; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-cost

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-cost: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

OBJDUMP=${OBJDUMP:-objdump}
NM=${NM:-nm}

for tool in "$OBJDUMP" "$NM"; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "borrow-cost: $tool not found" >&2
		exit 2
	fi
done

rm -rf "$OUT"
mkdir -p "$OUT/borrow" "$OUT/plain"
cp "$DIR/b08-borrow-cost.c" "$OUT/borrow/twin.c"
cp "$DIR/b08-borrow-cost-plain.c" "$OUT/plain/twin.c"

status=0

if ! "$CRUGC" -O2 -fcrug-borrow -Wno-crug-unadopted-own -c "$OUT/borrow/twin.c" -o "$OUT/borrow.o" >"$OUT/borrow.log" 2>&1; then
	echo "borrow-cost: the borrowed twin does not compile" >&2
	head -3 "$OUT/borrow.log" >&2
	exit 1
fi

if ! "$CRUGC" -O2 -c "$OUT/plain/twin.c" -o "$OUT/plain.o" >"$OUT/plain.log" 2>&1; then
	echo "borrow-cost: the plain twin does not compile" >&2
	head -3 "$OUT/plain.log" >&2
	exit 1
fi

if ! cmp -s "$OUT/borrow.o" "$OUT/plain.o"; then
	echo "borrow-cost: the borrow object differs from the plain-pointer object" >&2
	status=1
fi

calls=$("$OBJDUMP" -d "$OUT/borrow.o" | grep -cE '(call|jmp)[[:space:]].*__crug_' || true)
syms=$("$NM" "$OUT/borrow.o" | grep -c '__crug_' || true)

if [ "$calls" -ne 0 ] || [ "$syms" -ne 0 ]; then
	echo "borrow-cost: $calls helper calls, $syms helper symbols" >&2
	status=1
fi

if [ "$status" -eq 0 ]; then
	echo "borrow-cost: 2 functions, $calls calls, identical objects"
else
	echo "borrow-cost: 2 functions, $calls calls, different objects"
fi

exit "$status"
