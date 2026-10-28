#!/bin/sh
# check-channels.sh - the Phase 16 channel and select checks.
#
# Runs the channel DejaGnu suite, then compiles and runs the channel programs
# under libcrug/tests/channels against the Linux task runtime, then checks the
# runtime counters: sends and receives balance and no channel is lost
# (ADR 0025).
#
# Usage: scripts/check-channels.sh
# Exit: 0 every check passes; 1 a check fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=$ROOT/gcc/crugc
BUILD=$ROOT/build/channels
LIB=$ROOT/libcrug
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o $LIB/build/linux/chan.o"
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum
fail=0

mkdir -p "$BUILD"

fail_msg () {
	echo "channels: $1" >&2
	fail=1
}

# 1. The runtime objects the programs link against.
if ! make -C "$LIB" PAL=linux >"$BUILD/runtime.log" 2>&1; then
	echo "channels: make -C libcrug PAL=linux failed; see $BUILD/runtime.log" >&2
	cat "$BUILD/runtime.log" >&2
	exit 1
fi
for obj in $RUNTIME; do
	if [ ! -f "$obj" ]; then
		echo "channels: missing runtime object $obj" >&2
		exit 1
	fi
done

# 2. The compiler suite: compile-only cases with their own dg-options.
if ! make -C "$ROOT" check-CHANNELS_DG >"$BUILD/dg.log" 2>&1; then
	fail_msg "make check-CHANNELS_DG failed; see $BUILD/dg.log"
fi

dg_pass=$(grep -c '^PASS: crug.dg/channels/' "$SUM" 2>/dev/null || true)
dg_fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/channels/' "$SUM" 2>/dev/null || true)
# A run without a built compiler leaves gcc.sum missing and both counts empty;
# keep the arithmetic well formed, the message below names the real problem.
dg_pass=${dg_pass:-0}
dg_fail=${dg_fail:-0}
if [ "$dg_pass" -eq 0 ]; then
	fail_msg "no crug.dg/channels tests ran"
fi
if [ "$dg_fail" -ne 0 ]; then
	fail_msg "$dg_fail crug.dg/channels test(s) failed"
fi

# 3. Compile, link and run the channel programs.
progs=0
for src in "$LIB"/tests/channels/*.c; do
	[ -f "$src" ] || continue
	name=$(basename "$src" .c)
	flags="-O2 -fcrug-channels"
	if grep -q ' own ' "$src"; then
		flags="$flags -fcrug-ownership"
	fi
	# The word splitting above is intended: $flags is a flag list.
	if ! $CC $flags -I"$LIB/core" "$src" -o "$BUILD/$name" $RUNTIME -pthread >"$BUILD/$name.log" 2>&1; then
		fail_msg "cannot build $name; see $BUILD/$name.log"
		sed -n '1,10p' "$BUILD/$name.log" >&2
		continue
	fi
	if ! timeout 120 "$BUILD/$name" >>"$BUILD/$name.log" 2>&1; then
		fail_msg "$name failed"
		cat "$BUILD/$name.log" >&2
		continue
	fi
	grep '^channels-' "$BUILD/$name.log" | tail -n 1
	progs=$((progs + 1))
done
if [ "$progs" -eq 0 ]; then
	fail_msg "no channel programs ran"
fi

# 4. The over-aligned receive arm (C-01).  The element type below is 64-byte
# aligned, while the compatibility receive scratch is only 16-byte aligned.
# The compiler must receive into a temporary of the element type; if it falls
# back to the pointer form, UBSan reports a misaligned load.  The in-tree
# driver has no libubsan beside it, so the sanitized object is linked by the
# host C compiler (the runtime objects are plain C).
HOSTCC=${HOSTCC:-cc}
align_src="$LIB/tests/channels/recv-align.c"
align_obj="$BUILD/recv-align-ubsan.o"
align_bin="$BUILD/recv-align-ubsan"
align_log="$BUILD/recv-align-ubsan.log"
rm -f "$align_obj" "$align_bin" "$align_log"
if ! "$CC" -O2 -fcrug-channels -fsanitize=undefined \
	-fno-sanitize-recover=alignment -I"$LIB/core" \
	-c "$align_src" -o "$align_obj" >"$align_log" 2>&1; then
	fail_msg "cannot build recv-align for UBSan; see $align_log"
	sed -n '1,10p' "$align_log" >&2
elif ! command -v "$HOSTCC" >/dev/null 2>&1; then
	fail_msg "cannot run the UBSan arm: no host C compiler (HOSTCC=$HOSTCC)"
elif ! "$HOSTCC" -no-pie -fsanitize=undefined \
	-fno-sanitize-recover=alignment -o "$align_bin" "$align_obj" \
	$RUNTIME -pthread >>"$align_log" 2>&1; then
	fail_msg "cannot link recv-align for UBSan; see $align_log"
	sed -n '1,10p' "$align_log" >&2
elif ! timeout 120 env UBSAN_OPTIONS=halt_on_error=1 \
	"$align_bin" >>"$align_log" 2>&1; then
	fail_msg "recv-align UBSan run failed (misaligned receive?)"
	cat "$align_log" >&2
elif grep -q 'runtime error' "$align_log"; then
	fail_msg "recv-align UBSan reported a runtime error"
	grep 'runtime error' "$align_log" >&2
else
	align_line=$(grep '^channels-' "$align_log" | tail -n 1)
	if [ -z "$align_line" ]; then
		fail_msg "recv-align printed no channels- result line"
	else
		echo "channels-ubsan-${align_line#channels-}"
	fi
fi

echo "channels: $dg_pass compiler test(s), $progs channel program(s), $fail failure(s)"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0