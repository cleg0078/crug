#!/bin/sh
# check-borrow-nll.sh - a borrow ends at its last use, and the dump says so.
#
# Phase 6 ended every borrow at the end of its block.  Phase 7 ends it at the
# last statement that reads it, which is what lets the owner be moved, dropped
# or mutably borrowed afterwards.  This check compiles two programs that are
# only accepted with last-use liveness and inspects the borrow dump: it must
# contain at least one "(last-use)" end and at least one scope-end end, so the
# two reasons stay distinguishable.
#
# Usage: scripts/check-borrow-nll.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 both ends were dumped; 1 a program was rejected or an end is missing;
#       2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-nll

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-nll: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

bad=0
last=0
scope=0

for fixture in b10-last-use-shared.c b12-last-use-drop.c; do
	base=$(echo "$fixture" | sed 's/\.c$//')
	if ! "$CRUGC" -O2 -fcrug-borrow -Wno-crug-unadopted-own \
		-fcrug-dump=borrow -c "$DIR/$fixture" -o "$OUT/$base.o" \
		>"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: rejected (%s)\n' "$fixture" \
			"$(grep -m1 -E 'error:' "$OUT/$base.log")" >&2
		bad=$((bad + 1))
		continue
	fi
	dump=$OUT/$base.crug-borrow
	if [ ! -f "$dump" ]; then
		printf 'FAIL %s: no borrow dump written\n' "$fixture" >&2
		bad=$((bad + 1))
		continue
	fi
	last=$((last + $(grep -c '^end .*(last-use)$' "$dump")))
	scope=$((scope + $(grep -c '^end ' "$dump") - $(grep -c '^end .*(last-use)$' "$dump")))
done

if [ "$last" -lt 1 ]; then
	echo "FAIL: no last-use end in the dump" >&2
	bad=$((bad + 1))
fi
if [ "$scope" -lt 1 ]; then
	echo "FAIL: no scope-end end in the dump" >&2
	bad=$((bad + 1))
fi

echo "borrow-nll: last-use $last, scope-end $scope"

if [ "$bad" -ne 0 ]; then
	exit 1
fi
exit 0
