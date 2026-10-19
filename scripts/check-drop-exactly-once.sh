#!/bin/sh
# check-drop-exactly-once.sh - every owned value is destroyed exactly once.
#
# Compiles and runs the counting programs under
# gcc/testsuite/crug.dg/drop/exactly-once/.  Each program defines its own
# allocator and destructor and exits non-zero when a count is wrong, so the
# check needs no library and no sanitizer.
#
# Usage: scripts/check-drop-exactly-once.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 every program reports the expected counts; 1 a count is wrong; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/drop/exactly-once
OUT=$ROOT/build/drop-exactly-once
FLAGS="-O2 -fcrug-ownership"

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "exactly-once: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

if ! "$CRUGC" --version >/dev/null 2>&1; then
	echo "exactly-once: $CRUGC cannot run; build the compiler first" >&2
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
	if ! "$CRUGC" $FLAGS "$src" -o "$OUT/$base" >"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: does not compile\n' "$base"
		head -3 "$OUT/$base.log"
		bad=$((bad + 1))
		continue
	fi
	"$OUT/$base"
	rc=$?
	if [ "$rc" -ne 0 ]; then
		printf 'FAIL %s: exit %d, counts are wrong\n' "$base" "$rc"
		bad=$((bad + 1))
	fi
done

ok=$((total - bad))
echo "exactly-once: $ok/$total programs"

if [ "$bad" -ne 0 ] || [ "$total" -eq 0 ]; then
	exit 1
fi

exit 0
