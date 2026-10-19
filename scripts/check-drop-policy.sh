#!/bin/sh
# check-drop-policy.sh - the drop policy diagnostics are stable codes.
#
# Compiles one fixture per policy rule and requires the stable code (not a
# free-text message) on stderr.  These codes are the contract of
# docs/crug/DIAGNOSTICS.md.
#
# Usage: scripts/check-drop-policy.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 all four rules report their code; 1 one is missing; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/drop
OUT=$ROOT/build/drop-policy

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "drop-policy: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

total=0
bad=0

# name fixture expected-code options
check () {
	name=$1
	fixture=$2
	code=$3
	opts=$4
	total=$((total + 1))
	"$CRUGC" -O2 -fcrug-ownership $opts -c "$DIR/$fixture" -o "$OUT/$name.o" >"$OUT/$name.log" 2>&1
	if grep -q "\[$code\]" "$OUT/$name.log"; then
		printf 'PASS %s [%s]\n' "$name" "$code"
	else
		printf 'FAIL %s: no [%s] on stderr\n' "$name" "$code"
		head -3 "$OUT/$name.log"
		bad=$((bad + 1))
	fi
}

check goto       goto-into-scope.c  crug-DROP-002 ""
check setjmp     setjmp.c           crug-DROP-003 ""
check unadopted  unadopted.c        crug-OWN-007   ""
check operand    drop-not-owned.c   crug-DROP-001  ""

echo "drop-policy: goto=[crug-DROP-002] setjmp=[crug-DROP-003] unadopted=[crug-OWN-007] operand=[crug-DROP-001]"

if [ "$bad" -ne 0 ]; then
	exit 1
fi

exit 0
