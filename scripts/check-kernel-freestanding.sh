#!/bin/sh
# check-kernel-freestanding.sh - Phase 2: the freestanding kernel translation
# unit compiles with the exact gate flags and references no hosted runtime.
#
# Usage: scripts/check-kernel-freestanding.sh
# Exit: 0 clean; 1 compile failed or a runtime reference appeared; 2 tooling missing.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SRC=$ROOT/gcc/testsuite/crug.dg/kernel/freestanding.c
OUT=$ROOT/out
OBJ=$OUT/freestanding-gate.o

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "freestanding: no crugc driver found" >&2
	exit 2
fi

command -v nm >/dev/null 2>&1 || { echo "freestanding: nm not found" >&2; exit 2; }
command -v readelf >/dev/null 2>&1 || { echo "freestanding: readelf not found" >&2; exit 2; }

mkdir -p "$OUT"
if ! "$CRUGC" -fcrug-kernel -ffreestanding -nostdlib -c "$SRC" -o "$OBJ"; then
	echo "freestanding: compilation failed" >&2
	exit 1
fi

bad=0
undef=$OUT/freestanding-undef.txt
nm -u "$OBJ" | awk '{print $NF}' > "$undef"
if grep -Eq '^(malloc|calloc|realloc|free|pthread_|__cxa_)' "$undef"; then
	echo "freestanding: hosted runtime symbols referenced:" >&2
	grep -E '^(malloc|calloc|realloc|free|pthread_|__cxa_)' "$undef" >&2
	bad=1
fi
if readelf -S "$OBJ" | grep -q '\.init_array'; then
	echo "freestanding: .init_array is present" >&2
	bad=1
fi

if [ "$bad" -eq 0 ]; then
	echo "freestanding: no runtime symbols, no init_array"
	exit 0
fi
exit 1
