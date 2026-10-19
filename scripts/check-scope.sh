#!/bin/sh
# check-scope.sh - the Phase 15 structured-scope checks.
#
# Runs the 'scope' DejaGnu suite, then compiles and runs the task-group
# programs under libcrug/tests/scope against the Linux task runtime, then
# checks the group counters: every scope waits for its children and releases
# its state (ADR 0024).
#
# Usage: scripts/check-scope.sh
# Exit: 0 every check passes; 1 a check fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=$ROOT/gcc/crugc
BUILD=$ROOT/build/scope
LIB=$ROOT/libcrug
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o"
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum
fail=0

mkdir -p "$BUILD"

fail_msg () {
	echo "scope: $1" >&2
	fail=1
}

# 1. The runtime objects the programs link against.
if ! make -C "$LIB" PAL=linux >"$BUILD/runtime.log" 2>&1; then
	echo "scope: make -C libcrug PAL=linux failed; see $BUILD/runtime.log" >&2
	cat "$BUILD/runtime.log" >&2
	exit 1
fi
for obj in $RUNTIME; do
	if [ ! -f "$obj" ]; then
		echo "scope: missing runtime object $obj" >&2
		exit 1
	fi
done

# 2. The compiler suite: compile-only cases with their own dg-options.
if ! make -C "$ROOT" check-SCOPE_DG >"$BUILD/dg.log" 2>&1; then
	fail_msg "make check-SCOPE_DG failed; see $BUILD/dg.log"
fi

dg_pass=$(grep -c '^PASS: crug.dg/scope/' "$SUM" 2>/dev/null || true)
dg_fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/scope/' "$SUM" 2>/dev/null || true)
if [ "$dg_pass" -eq 0 ]; then
	fail_msg "no crug.dg/scope tests ran"
fi
if [ "$dg_fail" -ne 0 ]; then
	fail_msg "$dg_fail crug.dg/scope test(s) failed"
fi

# 3. Compile, link and run the task-group programs.
progs=0
for src in "$LIB"/tests/scope/*.c; do
	[ -f "$src" ] || continue
	name=$(basename "$src" .c)
	flags="-O2 -fcrug-goroutines"
	if grep -q ' own ' "$src"; then
		flags="$flags -fcrug-ownership"
	fi
	if grep -qE '\b(mut|ref|borrow|borrow_mut)\b' "$src"; then
		flags="$flags -fcrug-borrow"
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
	grep '^scope-' "$BUILD/$name.log" | tail -n 1
	progs=$((progs + 1))
done
if [ "$progs" -eq 0 ]; then
	fail_msg "no task-group programs ran"
fi

# 4. No group state survives: the stress program reports the live count.
leaked=$(grep -h 'leaked task group state:' "$BUILD"/*.log 2>/dev/null | tail -n 1 | sed 's/.*: //')
[ -n "$leaked" ] || leaked=none
if [ "$leaked" != "0" ]; then
	fail_msg "task group state survives the stress run (leaked: $leaked)"
fi

echo "scope: $dg_pass compiler test(s), $progs group program(s), $leaked leaked group(s), $fail failure(s)"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0
