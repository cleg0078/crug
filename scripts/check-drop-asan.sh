#!/bin/sh
# check-drop-asan.sh - no leaks or invalid frees under AddressSanitizer.
#
# Compiles the programs under gcc/testsuite/crug.dg/drop/asan/ with crugc (so
# the drop lowering runs) and links them against the host AddressSanitizer.
# The objects are not instrumented, so this detects leaks and invalid or
# duplicate frees through the runtime interceptors, not out-of-bounds accesses.
#
# Usage: scripts/check-drop-asan.sh
# Environment: CRUGC overrides the driver path; HOSTCC overrides the linker.
# Exit: 0 all programs exit 0 with no sanitizer report; 1 a report appeared;
#       2 no driver, no host compiler, or no AddressSanitizer runtime.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/drop/asan
OUT=$ROOT/build/drop-asan

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "drop-asan: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

if [ -n "${HOSTCC:-}" ]; then
	:
elif command -v gcc >/dev/null 2>&1; then
	HOSTCC=gcc
else
	echo "drop-asan: no host compiler to link the sanitizer runtime" >&2
	exit 2
fi

if ! echo 'int main (void) { return 0; }' | "$HOSTCC" -x c -fsanitize=address - -o /dev/null >/dev/null 2>&1; then
	echo "drop-asan: $HOSTCC has no AddressSanitizer runtime" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

total=0
bad=0

for src in "$DIR"/*.c; do
	[ -e "$src" ] || continue
	base=$(basename "$src" .c)
	total=$((total + 1))
	if ! "$CRUGC" -O1 -g -fno-pie -fcrug-ownership -c "$src" -o "$OUT/$base.o" >"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: does not compile\n' "$base"
		head -3 "$OUT/$base.log"
		bad=$((bad + 1))
		continue
	fi
	if ! "$HOSTCC" -no-pie -fsanitize=address "$OUT/$base.o" -o "$OUT/$base" >"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: does not link with AddressSanitizer\n' "$base"
		head -3 "$OUT/$base.log"
		bad=$((bad + 1))
		continue
	fi
	if ! ASAN_OPTIONS=detect_leaks=1 "$OUT/$base" >"$OUT/$base.out" 2>&1; then
		printf 'FAIL %s: sanitizer report or non-zero exit\n' "$base"
		grep -E 'ERROR: (AddressSanitizer|LeakSanitizer)|SUMMARY' "$OUT/$base.out" | head -3
		bad=$((bad + 1))
		continue
	fi
	if grep -qE 'ERROR: (AddressSanitizer|LeakSanitizer)' "$OUT/$base.out"; then
		printf 'FAIL %s: sanitizer report\n' "$base"
		grep -E 'ERROR: (AddressSanitizer|LeakSanitizer)|SUMMARY' "$OUT/$base.out" | head -3
		bad=$((bad + 1))
	fi
done

ok=$((total - bad))
echo "drop-asan: $ok/$total programs, $bad with reports"

if [ "$bad" -ne 0 ] || [ "$total" -eq 0 ]; then
	exit 1
fi

exit 0
