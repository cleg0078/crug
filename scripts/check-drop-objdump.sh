#!/bin/sh
# check-drop-objdump.sh - drop lowering introduces no helper calls.
#
# Compiles programs that destroy owned values on every exit kind and checks the
# generated objects for Crug helper symbols.  A drop is a direct null-guarded
# call to the destructor; there is no runtime, no flag word and no helper.
#
# Usage: scripts/check-drop-objdump.sh
# Environment: CRUGC overrides the driver path; OBJDUMP, NM override binutils.
# Exit: 0 no helper call or symbol; 1 one was found; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/drop
OUT=$ROOT/build/drop-objdump
FLAGS="-O2 -fcrug-ownership -c"

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "drop-objdump: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

OBJDUMP=${OBJDUMP:-objdump}
NM=${NM:-nm}

for tool in "$OBJDUMP" "$NM"; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "drop-objdump: $tool not found" >&2
		exit 2
	fi
done

rm -rf "$OUT"
mkdir -p "$OUT"

total=0
bad=0

for base in scope-end early-return branch-return goto-out adopt-free; do
	src=$DIR/$base.c
	if [ ! -f "$src" ] && [ -f "$DIR/exactly-once/$base.c" ]; then
		src=$DIR/exactly-once/$base.c
	fi
	if [ ! -f "$src" ]; then
		printf 'FAIL %s: fixture missing\n' "$base"
		bad=$((bad + 1))
		continue
	fi
	total=$((total + 1))
	if ! "$CRUGC" $FLAGS "$src" -o "$OUT/$base.o" >"$OUT/$base.log" 2>&1; then
		printf 'FAIL %s: does not compile\n' "$base"
		head -3 "$OUT/$base.log"
		bad=$((bad + 1))
		continue
	fi
	calls=$("$OBJDUMP" -d "$OUT/$base.o" | grep -cE '(call|jmp)[[:space:]].*__crug_' || true)
	syms=$("$NM" "$OUT/$base.o" | grep -c '__crug_' || true)
	if [ "$calls" -ne 0 ] || [ "$syms" -ne 0 ]; then
		printf 'FAIL %s: %s helper calls, %s helper symbols\n' "$base" "$calls" "$syms"
		bad=$((bad + 1))
	fi
done

echo "drop-objdump: $total objects, $bad with __crug_ calls"

if [ "$bad" -ne 0 ] || [ "$total" -eq 0 ]; then
	exit 1
fi

exit 0
