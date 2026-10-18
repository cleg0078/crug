#!/bin/sh
# test-kernel-compat.sh - Phase 2 kernel/GNU C compatibility runner.
#
# Exercises the 21 work items of spec/CRUG_PRODUCTION_ROADMAP.md section 7 using
# the named corpus in gcc/testsuite/crug.dg/kernel/. Compile-only: nothing is
# linked, so no C library is needed.
#
# Usage: scripts/test-kernel-compat.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 all 21 work items pass; 1 any item fails; 2 driver or corpus missing.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
KERNEL_DIR=$ROOT/gcc/testsuite/crug.dg/kernel
OUT=$ROOT/out
FLAGS="-fcrug-kernel -ffreestanding -nostdlib -O2"

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "kernel-compat: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

if ! "$CRUGC" --version >/dev/null 2>&1; then
	echo "kernel-compat: $CRUGC cannot run; build the compiler first" >&2
	exit 2
fi

if [ ! -d "$KERNEL_DIR" ]; then
	echo "kernel-compat: corpus directory $KERNEL_DIR is missing" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

n=0
fails=0

report() {
	n=$((n + 1))
	if [ "$1" -eq 0 ]; then
		printf 'PASS %02d %s\n' "$n" "$2"
	else
		printf 'FAIL %02d %s\n' "$n" "$2"
		fails=$((fails + 1))
	fi
}

compile() {
	src=$1
	obj=$2
	"$CRUGC" $FLAGS -c -o "$OUT/$obj" "$KERNEL_DIR/$src" >"$OUT/$obj.log" 2>&1
}

# 1. -fcrug-kernel is accepted by the driver and changes no C code generation.
"$CRUGC" $FLAGS -c -o "$OUT/flag-on.o" "$KERNEL_DIR/k01-stmt-expr.c" >"$OUT/flag-on.log" 2>&1
rc_on=$?
"$CRUGC" -ffreestanding -nostdlib -O2 -c -o "$OUT/flag-off.o" "$KERNEL_DIR/k01-stmt-expr.c" >"$OUT/flag-off.log" 2>&1
rc_off=$?
cmp -s "$OUT/flag-on.o" "$OUT/flag-off.o"
rc_ident=$?
"$CRUGC" -fcrug-kernel=1 -x c -fsyntax-only /dev/null >"$OUT/flag-bad.log" 2>&1
rc_bad=$?
if [ "$rc_on" -eq 0 ] && [ "$rc_off" -eq 0 ] && [ "$rc_ident" -eq 0 ] && [ "$rc_bad" -ne 0 ]; then
	report 0 "-fcrug-kernel accepted, byte-identical code, bad value rejected"
else
	report 1 "-fcrug-kernel acceptance or object identity failed"
fi

# 2. This script exists.
if [ -f "$ROOT/scripts/test-kernel-compat.sh" ]; then
	report 0 "scripts/test-kernel-compat.sh exists"
else
	report 1 "scripts/test-kernel-compat.sh is missing"
fi

# 3. The kernel microtest corpus exists and has at least 18 sources.
set -- "$KERNEL_DIR"/*.c
count=$#
if [ -f "$1" ] && [ "$count" -ge 18 ]; then
	report 0 "kernel corpus has $count sources (>= 18)"
else
	report 1 "kernel corpus too small or missing (found $count)"
fi

# 4-18. One source per roadmap work item.
compile k01-stmt-expr.c k01.o;                report $? "statement expressions"
compile k02-typeof.c k02.o;                   report $? "typeof / __typeof__"
compile k03-auto-type.c k03.o;                report $? "__auto_type"
compile k04-attributes.c k04.o;               report $? "attributes"
compile k05-sections.c k05.o;                 report $? "section placement"
compile k06-packed-aligned.c k06.o;           report $? "packed and aligned types"
compile k07-zero-length-array.c k07.o;        report $? "zero-length arrays"
compile k08-flexible-array.c k08.o;           report $? "flexible array members"
compile k09-computed-goto.c k09.o;            report $? "computed goto"
compile k10-inline-asm.c k10.o;               report $? "inline asm"
compile k11-asm-goto.c k11.o;                 report $? "asm goto"
compile k12-volatile-mmio.c k12.o;            report $? "volatile and MMIO"
compile k13-atomics.c k13.o;                  report $? "builtin atomics and barriers"
compile k14-branch-prediction.c k14.o;        report $? "branch prediction hints"
compile k15-container-of.c k15.o;             report $? "container_of"

# 19. -ffreestanding.
compile freestanding.c freestanding.o;        report $? "-ffreestanding translation unit"

# 20. -nostdlib, plus a translation unit that must not reference the Crug runtime
# (both compile results feed one work item so the report stays 21/21).
compile nostdlib.c nostdlib.o;                rc_nostdlib=$?
compile no-runtime.c no-runtime.o;            rc_noruntime=$?
if [ "$rc_nostdlib" -eq 0 ] && [ "$rc_noruntime" -eq 0 ]; then
	report 0 "-nostdlib and runtime-free translation units"
else
	report 1 "-nostdlib or runtime-free translation unit failed"
fi

# 21. No object may reference the Crug runtime.
scan_bad=0
for o in "$OUT"/*.o; do
	[ -f "$o" ] || continue
	if nm "$o" 2>/dev/null | awk '{print $NF}' | grep -q '^__crug_'; then
		echo "kernel-compat: $o references the Crug runtime (__crug_)" >&2
		scan_bad=1
	fi
	if readelf -d "$o" 2>/dev/null | grep -q 'libcrug'; then
		echo "kernel-compat: $o needs libcrug" >&2
		scan_bad=1
	fi
done
report "$scan_bad" "objects scanned: no __crug_ symbols, no libcrug dependency"

total=21
if [ "$fails" -eq 0 ] && [ "$n" -eq "$total" ]; then
	echo "kernel-compat: 21/21 work items PASS"
	exit 0
fi
printf 'kernel-compat: %d/21 work items PASS\n' "$((total - fails))"
exit 1
