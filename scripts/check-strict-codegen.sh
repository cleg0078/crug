#!/bin/sh
# check-strict-codegen.sh - -fcrug-strict changes no code.
#
# Compiles gcc/testsuite/crug.dg/strict/clean/ twice - once with
# -fcrug-ownership and once with -fcrug-strict, which is that plus the strict
# checks - and requires byte-identical objects.  A difference means a strict
# check leaked into code generation (ADR 0021 D1: strict mode is a checking
# mode, never a code generation mode).
#
# Usage: scripts/check-strict-codegen.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 no differences; 1 a difference; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/strict/clean
OUT=$ROOT/build/strict-codegen

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "strict-codegen: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT/plain" "$OUT/strict"

n=0
differ=0
for src in "$DIR"/*.c; do
	test -e "$src" || continue
	n=$((n + 1))
	base=$(basename "$src" .c)
	"$CRUGC" -O2 -fcrug-ownership -c "$src" -o "$OUT/plain/$base.o" 2>"$OUT/plain/$base.log"
	plain=$?
	"$CRUGC" -O2 -fcrug-strict -c "$src" -o "$OUT/strict/$base.o" 2>"$OUT/strict/$base.log"
	strictrc=$?
	if [ "$plain" -ne 0 ] || [ "$strictrc" -ne 0 ]; then
		printf 'FAIL %s: compile failed (%s/%s)\n' "$base" "$plain" "$strictrc" >&2
		head -3 "$OUT/plain/$base.log" "$OUT/strict/$base.log" >&2
		differ=$((differ + 1))
		continue
	fi
	if ! cmp -s "$OUT/plain/$base.o" "$OUT/strict/$base.o"; then
		printf 'FAIL %s: objects differ\n' "$base" >&2
		differ=$((differ + 1))
	fi
done

if [ "$n" -eq 0 ]; then
	echo "strict-codegen: no corpus files under $DIR" >&2
	exit 2
fi

echo "strict-codegen: $n programs, $differ differences"

if [ "$differ" -ne 0 ]; then
	exit 1
fi

exit 0
