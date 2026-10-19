#!/bin/sh
# check-strict-policy.sh - every strict restriction reports its stable code.
#
# Compiles one fixture per restriction with -fcrug-strict and requires the
# documented code in stderr: a raw dereference (SAFE-001), an integer-pointer
# conversion (SAFE-002), an owned value converted to an unmanaged pointer
# (SAFE-003), an asm statement (SAFE-004), a non-local exit (SAFE-005), and the
# two call-site contract errors (OWN-011, OWN-012).
#
# Usage: scripts/check-strict-policy.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 every code was reported; 1 one was missing; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/strict
OUT=$ROOT/build/strict-policy

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "strict-policy: no crugc driver found (build it with make baseline)" >&2
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
	if ! "$CRUGC" -O2 -fcrug-strict -fsyntax-only "$DIR/$fixture" \
		>"$OUT/$fixture.log" 2>&1; then
		if grep -q "crug-$code" "$OUT/$fixture.log"; then
			summary="$summary $code"
		else
			printf 'FAIL %s: crug-%s missing\n' "$fixture" "$code" >&2
			head -3 "$OUT/$fixture.log" >&2
			bad=$((bad + 1))
		fi
	else
		printf 'FAIL %s: compiled but should be rejected by strict mode\n' "$fixture" >&2
		bad=$((bad + 1))
	fi
}

check raw-deref.c SAFE-001
check int-to-pointer.c SAFE-002
check owned-to-pointer.c SAFE-003
check asm-outside-unsafe.c SAFE-004
check setjmp-outside-unsafe.c SAFE-005
check own-arg-own-param.c OWN-011
check own-arg-plain-call.c OWN-012

echo "strict-policy:$summary"

if [ "$bad" -ne 0 ]; then
	exit 1
fi

exit 0
