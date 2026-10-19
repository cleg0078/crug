#!/bin/sh
# check-goroutines.sh - the Phase 14 compiler-side goroutine checks.
#
# Runs the 'go'/'yield' DejaGnu suite, then compiles and runs the Crug task
# programs under libcrug/tests/goroutines against the Linux task runtime, then
# checks that a stress run starts one operating-system thread per worker and
# not one per task (ADR 0008, ADR 0009).
#
# Usage: scripts/check-goroutines.sh
# Exit: 0 every check passes; 1 a check fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=$ROOT/gcc/crugc
BUILD=$ROOT/build/goroutines
LIB=$ROOT/libcrug
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o"
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum
fail=0

mkdir -p "$BUILD"

fail_msg () {
	echo "goroutines: $1" >&2
	fail=1
}

# 1. The runtime tests, which also build the objects the programs link against.
if ! make -C "$LIB" PAL=linux check-tasks >"$BUILD/runtime.log" 2>&1; then
	echo "goroutines: make -C libcrug PAL=linux check-tasks failed; see $BUILD/runtime.log" >&2
	cat "$BUILD/runtime.log" >&2
	exit 1
fi

for obj in $RUNTIME; do
	if [ ! -f "$obj" ]; then
		echo "goroutines: missing runtime object $obj" >&2
		exit 1
	fi
done

# 2. The compiler suite: compile-only cases with their own dg-options.
if ! make -C "$ROOT" check-GOROUTINES_DG >"$BUILD/dg.log" 2>&1; then
	fail_msg "make check-GOROUTINES_DG failed; see $BUILD/dg.log"
fi

dg_pass=$(grep -c '^PASS: crug.dg/goroutines/' "$SUM" 2>/dev/null || true)
dg_fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/goroutines/' "$SUM" 2>/dev/null || true)
if [ "$dg_pass" -eq 0 ]; then
	fail_msg "no crug.dg/goroutines tests ran"
fi
if [ "$dg_fail" -ne 0 ]; then
	fail_msg "$dg_fail crug.dg/goroutines test(s) failed"
fi

# 3. Compile, link and run the task programs.
progs=0
for src in "$LIB"/tests/goroutines/*.c; do
	[ -f "$src" ] || continue
	name=$(basename "$src" .c)
	flags="-O2 -fcrug-goroutines"
	if grep -q ' own ' "$src"; then
		flags="$flags -fcrug-ownership"
	fi
	# The word splitting above is intended: $flags is a flag list.
	if ! $CC $flags -I"$LIB/core" "$src" -o "$BUILD/$name" $RUNTIME -pthread >"$BUILD/$name.log" 2>&1; then
		fail_msg "cannot build $name; see $BUILD/$name.log"
		sed -n '1,10p' "$BUILD/$name.log" >&2
		continue
	fi
	if ! "$BUILD/$name" >>"$BUILD/$name.log" 2>&1; then
		fail_msg "$name failed"
		cat "$BUILD/$name.log" >&2
		continue
	fi
	grep '^goroutines-' "$BUILD/$name.log" | tail -n 1
	progs=$((progs + 1))
done
if [ "$progs" -eq 0 ]; then
	fail_msg "no task programs ran"
fi

# 4. One operating-system thread per worker, not one per task.
clone=$(CRUG_TASKS_WORKERS=4 GOROUTINES_STRESS=1 strace -f -e trace=clone,clone3 "$ROOT/build/tasks-stress" 2>&1 | grep -c clone || true)
if [ "$clone" -ne 4 ]; then
	fail_msg "a stress run of 100000 tasks made $clone clone calls, expected 4 worker threads"
fi

echo "goroutines: $dg_pass compiler test(s), $progs task program(s), 100000 tasks on 4 worker threads, $fail failure(s)"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0
