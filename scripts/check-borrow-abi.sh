#!/bin/sh
# check-borrow-abi.sh - ref and mut are erased at every ABI boundary.
#
# Compiles the assertion fixture (sizeof and _Alignof of a borrow equal those of
# a plain pointer) and compares a borrowed function with its plain twin at the
# object level.  A borrow changes no layout, no calling representation and adds
# no helper (ADR 0003 D3, RFC-0001 section 22).
#
# Both twins are copied to the same file name so their object files match.
#
# Usage: scripts/check-borrow-abi.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 the assertions hold and the objects match; 1 a difference; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/borrow
OUT=$ROOT/build/borrow-abi

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "borrow-abi: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT/plain" "$OUT/borrow"

if ! "$CRUGC" -O2 -fcrug-borrow -c "$DIR/b09-borrow-abi.c" -o "$OUT/abi.o" >"$OUT/abi.log" 2>&1; then
	echo "borrow-abi: b09-borrow-abi.c does not compile" >&2
	head -3 "$OUT/abi.log" >&2
	exit 1
fi

cat >"$OUT/plain/twin.c" <<'EOF'
extern int consume_plain (int v);
int read_plain (int *p)
{
  return *p + consume_plain (*p);
}
EOF

cat >"$OUT/borrow/twin.c" <<'EOF'
extern int consume_plain (int v);
int read_plain (ref int *p)
{
  return *p + consume_plain (*p);
}
EOF

if ! "$CRUGC" -O2 -c "$OUT/plain/twin.c" -o "$OUT/plain.o" >"$OUT/plain.log" 2>&1; then
	echo "borrow-abi: the plain twin does not compile" >&2
	head -3 "$OUT/plain.log" >&2
	exit 1
fi

if ! "$CRUGC" -O2 -fcrug-borrow -c "$OUT/borrow/twin.c" -o "$OUT/borrow.o" >"$OUT/borrow.log" 2>&1; then
	echo "borrow-abi: the borrowed twin does not compile" >&2
	head -3 "$OUT/borrow.log" >&2
	exit 1
fi

if ! cmp -s "$OUT/plain.o" "$OUT/borrow.o"; then
	echo "borrow-abi: the borrowed function differs from its plain twin" >&2
	exit 1
fi

echo "borrow-abi: 4 assertions, 0 differences"

exit 0
