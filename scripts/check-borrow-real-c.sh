#!/bin/sh
# check-borrow-real-c.sh - realistic programs compile and run under the borrow
# checker, with no internal compiler error.
#
# The programs under gcc/testsuite/crug.dg/borrow/real-c/ are ordinary small
# programs (a list, a string buffer, a table, a ring, a tokenizer, a callback
# dispatcher) that use borrows the way application code would.  They are not in
# the dg suite because they are too big to be single-file tests with one
# diagnostic; this check compiles and runs each of them.
#
# Usage: scripts/check-borrow-real-c.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 every program compiled and exited 0; 1 otherwise; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow/real-c
OUT=$ROOT/build/borrow-real-c

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-real-c: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

bad=0
ice=0
count=0

for source in list/list.c string/strbuf.c table/table.c ring/ring.c \
	parse/tokenizer.c api/callback.c; do
	base=$(echo "$source" | tr '/' '_' | sed 's/\.c$//')
	count=$((count + 1))
	if ! "$CRUGC" -O2 -fcrug-borrow -Wno-crug-unadopted-own \
		"$DIR/$source" -o "$OUT/$base" >"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: did not compile\n' "$source" >&2
		grep -m3 -E 'error:|internal compiler error' "$OUT/$base.log" >&2
		bad=$((bad + 1))
		if grep -q 'internal compiler error' "$OUT/$base.log"; then
			ice=$((ice + 1))
		fi
		continue
	fi
	if ! "$OUT/$base"; then
		printf 'FAIL %s: exited non-zero\n' "$source" >&2
		bad=$((bad + 1))
	fi
done

echo "borrow-real-c: $count programs, $ice ICE"

if [ "$bad" -ne 0 ]; then
	exit 1
fi
exit 0
