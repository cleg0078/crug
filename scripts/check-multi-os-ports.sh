#!/bin/sh
# check-multi-os-ports.sh - execute the runtime ports this host can run.
#
# The Phase 18 gate needs more than "it compiles on this host": the Windows
# x86-64 and Linux AArch64 ports were exactly where a two-worker task runtime
# failed (ADR 0009 A-1).  This script builds those ports with their real
# compilers and runs the full runtime suite on them, then repeats the task suite
# with 2, 4 and 8 workers, because the one-worker run cannot see a task that
# migrates between workers.
#
#   * Linux AArch64: a cross build plus qemu-aarch64, or the arm64 container
#     image crug-arm64-build (binfmt emulation) when it exists.
#   * Windows x86-64: a mingw-w64 cross build plus Wine, or the container image
#     crug-wine.
#   * Darwin: only on a macOS host; CI covers it otherwise (PORTABILITY.md §7).
#
# A port whose toolchain is absent is skipped and named in the summary; a port
# that was attempted and failed is a failure.  If every port is skipped the
# script exits 2, so the gate cannot pass on a machine that proved nothing.
#
# Usage: scripts/check-multi-os-ports.sh
# Exit: 0 every attempted port passed and at least one ran; 1 a port failed;
#       2 nothing could be run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LIB=$ROOT/libcrug
OUT=$ROOT/build/multi-os-ports
WIN=$OUT/windows
ARM=$OUT/arm64
LOG=$OUT/logs
WINE_IMAGE=crug-wine
ARM_IMAGE=crug-arm64-build
WINE_PREFIX=$OUT/wineprefix
fail=0
ran=0
ports=

mkdir -p "$OUT" "$LOG" "$WIN"

say () { echo "multi-os-ports: $*"; }

fail_msg () {
	say "$*" >&2
	fail=1
}

have () { command -v "$1" >/dev/null 2>&1; }

have_image () {
	have docker && docker image inspect "$1" >/dev/null 2>&1
}

# ---- Linux AArch64 -----------------------------------------------------

stage_arm64 () {
	rm -rf "$ARM"
	mkdir -p "$ARM"
	( cd "$LIB" && tar cf - --exclude build --exclude '*.o' . ) \
		| ( cd "$ARM" && tar xf - ) || return 1
	[ -f "$ARM/core/task.c" ] || return 1
	# The fix this script exists to protect (ADR 0009 A-1) must be present.
	grep -q 'worker_local_push' "$ARM/core/task.c" || {
		say "the staged core does not pin tasks to a worker (ADR 0009 A-1)" >&2
		return 1
	}
}

check_arm64 () {
	if have_image "$ARM_IMAGE"; then
		stage_arm64 || { fail_msg "aarch64 FAIL (cannot stage the source)"; return; }
		mkdir -p "$OUT/arm64-extbuild"
		# The Makefile writes the stress program to ../build; give the
		# container a writable mount there instead of its read-only root.
		docker run --rm --network host --user "$(id -u):$(id -g)" -e HOME=/tmp \
			-v "$ARM:/src" -v "$OUT/arm64-extbuild:/build" -w /src "$ARM_IMAGE" sh -c '
			set -e
			make check-MULTI_OS TARGET=aarch64-linux
			for w in 2 4 8; do
				CRUG_TASKS_WORKERS=$w ./build/linux-aarch64/tasks-test
			done' >"$LOG/arm64.log" 2>&1
		rc=$?
		grep -E '^(multi-os:|tasks:)' "$LOG/arm64.log" | tail -5
		if [ $rc -eq 0 ] && grep -q '^tasks: 17 checks, 0 failed' "$LOG/arm64.log"; then
			say "aarch64 PASS (Linux AArch64 PAL + AAPCS64 switch, 2/4/8 workers)"
			ports="$ports aarch64"
			ran=$((ran + 1))
		else
			fail_msg "aarch64 FAIL (see $LOG/arm64.log)"
		fi
	elif have aarch64-linux-gnu-gcc && have qemu-aarch64; then
		make -C "$LIB" check-MULTI_OS TARGET=aarch64-linux >"$LOG/arm64.log" 2>&1
		rc=$?
		if [ $rc -eq 0 ]; then
			for w in 2 4 8; do
				CRUG_TASKS_WORKERS=$w qemu-aarch64 -L /usr/aarch64-linux-gnu \
					"$LIB/build/linux-aarch64/tasks-test" >>"$LOG/arm64.log" 2>&1 \
					|| rc=1
			done
		fi
		grep -E '^(multi-os:|tasks:)' "$LOG/arm64.log" | tail -5
		if [ $rc -eq 0 ] && grep -q '^tasks: 17 checks, 0 failed' "$LOG/arm64.log"; then
			say "aarch64 PASS (Linux AArch64 PAL + AAPCS64 switch, 2/4/8 workers)"
			ports="$ports aarch64"
			ran=$((ran + 1))
		else
			fail_msg "aarch64 FAIL (see $LOG/arm64.log)"
		fi
	else
		say "aarch64 skipped (no $ARM_IMAGE image, no aarch64-linux-gnu-gcc + qemu-aarch64)"
	fi
}

# ---- Windows x86-64 ----------------------------------------------------

WFLAGS='-O2 -g -std=c11 -Wall -Wextra -Werror'

build_windows () {
	CROSS=x86_64-w64-mingw32-gcc
	CPP="-I$LIB/pal/include -I$LIB/core -I$LIB/arch/x86_64/windows -DCRUG_PAL_BACKEND=\"windows\""
	mkdir -p "$WIN"
	for unit in core/task.c core/chan.c core/pal_contract.c pal/windows/pal_windows.c arch/x86_64/windows/stack.c; do
		obj=$WIN/$(basename "$unit" .c).o
		# shellcheck disable=SC2086
		$CROSS $WFLAGS $CPP -c -o "$obj" "$LIB/$unit" || return 1
	done
	$CROSS $CPP -c -o "$WIN/switch.o" "$LIB/arch/x86_64/windows/switch.S" || return 1
	OBJS="$WIN/task.o $WIN/chan.o $WIN/pal_contract.o $WIN/pal_windows.o $WIN/stack.o $WIN/switch.o"
	# shellcheck disable=SC2086
	$CROSS $WFLAGS $CPP -o "$WIN/pal-contract.exe" \
		"$LIB/tests/pal_contract_main.c" $OBJS || return 1
	# shellcheck disable=SC2086
	$CROSS $WFLAGS $CPP -o "$WIN/tasks-test.exe" \
		"$LIB/tests/tasks_main.c" $OBJS || return 1
	for prog in chan_basic chan_handoff chan_select; do
		# shellcheck disable=SC2086
		$CROSS $WFLAGS $CPP -o "$WIN/$(echo "$prog" | tr '_' '-').exe" \
			"$LIB/tests/runtime/$prog.c" $OBJS || return 1
	done
	return 0
}

wine_run () {
	if have_image "$WINE_IMAGE"; then
		docker run --rm --network host --user "$(id -u):$(id -g)" \
			-e HOME=/tmp -e WINEPREFIX=/w/build/multi-os-ports/wineprefix \
			-e WINEDEBUG=-all -e CRUG_TASKS_WORKERS \
			-v "$ROOT:/w" crug-wine \
			sh -c "cd /w/build/multi-os-ports/windows && wine $*" 2>&1
	else
		WINEPREFIX=$WINE_PREFIX WINEDEBUG=-all wine "$@" 2>&1
	fi
}

check_windows () {
	if ! have x86_64-w64-mingw32-gcc; then
		say "windows skipped (no x86_64-w64-mingw32-gcc)"
		return
	fi
	if ! have_image "$WINE_IMAGE" && ! have wine; then
		say "windows skipped (cross compiler present, no Wine and no $WINE_IMAGE image)"
		return
	fi
	build_windows >"$LOG/windows-build.log" 2>&1 || {
		fail_msg "windows FAIL (cross build; see $LOG/windows-build.log)"
		return
	}
	{
		wine_run ./pal-contract.exe
		for w in 2 4 8; do
			CRUG_TASKS_WORKERS=$w wine_run ./tasks-test.exe
		done
		for prog in chan-basic chan-handoff chan-select; do
			wine_run "./$prog.exe"
		done
	} >"$LOG/windows.log" 2>&1
	rc=$?
	failures=$(grep -c '^tasks: 17 checks, 0 failed' "$LOG/windows.log")
	grep -E '^(pal-contract|tasks:|channels-)' "$LOG/windows.log" | tail -8
	if [ $rc -eq 0 ] && [ "$failures" -eq 3 ]; then
		say "windows PASS (Microsoft x64 switch under Wine, 2/4/8 workers)"
		ports="$ports windows"
		ran=$((ran + 1))
	else
		fail_msg "windows FAIL (see $LOG/windows.log)"
	fi
}

# ---- Darwin ------------------------------------------------------------

check_darwin () {
	if [ "$(uname -s)" = Darwin ]; then
		if make -C "$LIB" check-MULTI_OS >"$LOG/darwin.log" 2>&1; then
			say "darwin PASS (native macOS run)"
			ports="$ports darwin"
			ran=$((ran + 1))
		else
			fail_msg "darwin FAIL (see $LOG/darwin.log)"
		fi
	else
		say "darwin skipped (not a macOS host; CI covers it)"
	fi
}

check_arm64
check_windows
check_darwin

if [ -n "$ports" ]; then
	say "executed:$ports"
fi
if [ $fail -ne 0 ]; then
	say "FAILED"
	exit 1
fi
if [ $ran -eq 0 ]; then
	say "nothing to run: no port toolchain or container image on this host"
	exit 2
fi
say "ok: $ran port(s) executed"
exit 0
