#!/bin/sh
# check-borrow-dump.sh - the borrow dump records lenders and borrow ends.
#
# Compiles one program with shared borrows and one with a mutable borrow under
# -fcrug-dump=borrow and requires the "borrow" and "end" lines with the lender
# name.  The dump reports static facts, not a runtime structure.
#
# Usage: scripts/check-borrow-dump.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 the dumps have the expected shape; 1 they do not; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-dump

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-dump: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

for base in b01-multiple-shared b02-one-mut; do
	if ! "$CRUGC" -O0 -fcrug-borrow -Wno-crug-unadopted-own -fcrug-dump=borrow \
		-c "$DIR/$base.c" -o "$OUT/$base.o" >"$OUT/$base.log" 2>&1; then
		echo "borrow-dump: $base.c does not compile" >&2
		head -3 "$OUT/$base.log" >&2
		exit 1
	fi
done

if [ ! -f "$OUT/b01-multiple-shared.crug-borrow" ] || [ ! -f "$OUT/b02-one-mut.crug-borrow" ]; then
	echo "borrow-dump: no dump file next to the object" >&2
	exit 1
fi

shared=$(grep -c '^borrow .* shares ' "$OUT/b01-multiple-shared.crug-borrow" || true)
mutable=$(grep -c '^borrow .* excludes ' "$OUT/b02-one-mut.crug-borrow" || true)
ends=$(grep -h -c '^end ' "$OUT"/*.crug-borrow | awk '{ s += $1 } END { print s + 0 }')

bad=0
if [ "$shared" -lt 1 ]; then
	echo "borrow-dump: no shared borrow line" >&2
	bad=1
fi
if [ "$mutable" -lt 1 ]; then
	echo "borrow-dump: no mutable borrow line" >&2
	bad=1
fi
if [ "$ends" -lt 2 ]; then
	echo "borrow-dump: $ends end lines, want at least 2" >&2
	bad=1
fi

if [ "$bad" -ne 0 ]; then
	cat "$OUT"/*.crug-borrow >&2
	exit 1
fi

echo "borrow-dump: $shared shared, $mutable mutable, $ends ends"

exit 0
