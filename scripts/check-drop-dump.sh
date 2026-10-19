#!/bin/sh
# check-drop-dump.sh - -fcrug-dump=drop names every destruction.
#
# Compiles fixtures that destroy owned values on scope end, return, break,
# continue and goto, and requires one "drop <file>:<line>: <kind> <name> via
# <destructor>" line per kind.
#
# Usage: scripts/check-drop-dump.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 all five exit kinds appear; 1 one is missing; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/drop
OUT=$ROOT/build/drop-dump

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "drop-dump: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

total=0
bad=0

# fixture kind
check () {
	fixture=$1
	kind=$2
	total=$((total + 1))
	if ! "$CRUGC" -O2 -fcrug-ownership -fcrug-dump=drop -c "$DIR/$fixture.c" \
			-o "$OUT/$fixture.o" >"$OUT/$fixture.log" 2>&1; then
		printf 'FAIL %s: does not compile\n' "$fixture"
		head -3 "$OUT/$fixture.log"
		bad=$((bad + 1))
		return
	fi
	dumpfile=$OUT/$fixture.crug-drop
	if grep -qE "^drop .*: [a-z-]+ .* via " "$dumpfile" &&
	   grep -qE "^drop .*: $kind " "$dumpfile"; then
		printf 'PASS %s: exit kind %s\n' "$fixture" "$kind"
	else
		printf 'FAIL %s: no %s drop line in %s\n' "$fixture" "$kind" "$dumpfile"
		bad=$((bad + 1))
	fi
}

check scope-end     scope-end
check early-return  return
check break-drop    break
check continue-drop continue
check goto-out      goto

echo "drop-dump: scope-end return break continue goto 5 kinds"

if [ "$bad" -ne 0 ]; then
	exit 1
fi

exit 0
