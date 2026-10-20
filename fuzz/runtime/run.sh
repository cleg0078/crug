#!/bin/sh
# run.sh - Phase 22 runtime fuzz harnesses for the Crug runtime.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Two harnesses, both built into build/fuzz/runtime/:
#
#   scheduler_fuzz.c  randomized sequences against the public task/scope/
#                     channel API, built with gcc ASan+UBSan (manual stack
#                     switching is exercised, so ASan sees the switched
#                     stacks through the runtime's annotations).
#   channel_fuzz.c    a clang libFuzzer target that turns the input bytes
#                     into a bounded channel operation stream (create with a
#                     capacity, send, recv, close, select) and checks the
#                     FIFO/closed/readiness/stats invariants after every op.
#
# FUZZ_RUNS selects the budget for both harnesses (scheduler default 2000
# sequences, channel default 20000 runs).  FUZZ_SEED is passed to the
# scheduler harness so a reported sequence can be re-run alone.
# Exits 0 when both harnesses are clean, 1 on a report or crash, 2 when gcc,
# clang or a sanitizer is missing.  Safe to run from any directory.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 2
cd "$root" || exit 2

build=build/fuzz/runtime
mkdir -p "$build" || exit 2

# ---------------------------------------------------------------------------
# Prerequisites: gcc with ASan+UBSan, clang with libFuzzer+ASan+UBSan.
# ---------------------------------------------------------------------------
missing=0
for tool in gcc clang; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        printf 'runtime-fuzz: SKIP: %s is not installed\n' "$tool"
        missing=2
    fi
done
if [ "$missing" -ne 0 ]; then
    exit 2
fi

probe="$build/probe.c"
trap 'rm -f "$build/probe.c" "$build/probe-gcc" "$build/probe-clang"' 0 2 15
printf 'int main (void) { return 0; }\n' > "$probe"
if ! gcc -fsanitize=address,undefined -fno-omit-frame-pointer "$probe" \
        -o "$build/probe-gcc" >"$build/probe-gcc.log" 2>&1; then
    printf 'runtime-fuzz: SKIP: gcc does not support -fsanitize=address,undefined\n'
    exit 2
fi
printf 'int LLVMFuzzerTestOneInput (const unsigned char *d, unsigned long n)\n{ (void) d; (void) n; return 0; }\n' > "$probe"
if ! clang -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer "$probe" \
        -o "$build/probe-clang" >"$build/probe-clang.log" 2>&1; then
    printf 'runtime-fuzz: SKIP: clang does not support -fsanitize=fuzzer,address,undefined\n'
    exit 2
fi

asan='abort_on_error=1:detect_leaks=1'
ubsan='halt_on_error=1:print_stacktrace=1'
bad='ERROR: AddressSanitizer|ERROR: LeakSanitizer|ERROR: UndefinedBehaviorSanitizer|runtime error:|FUZZ-INVARIANT|SUMMARY: .*Sanitizer|FAIL: scheduler-fuzz'

if command -v timeout >/dev/null 2>&1; then
    timeout_cmd=timeout
else
    timeout_cmd=
fi

# ---------------------------------------------------------------------------
# Harness 1: the scheduler / scope / channel runtime under ASan and UBSan.
# ---------------------------------------------------------------------------
sched_runs=${FUZZ_RUNS:-2000}
if ! gcc -O1 -g -std=c11 -Wall -Wextra -Werror \
        -fsanitize=address,undefined -fno-omit-frame-pointer \
        -Ilibcrug/pal/include -Ilibcrug/core -Ilibcrug/arch/x86_64/sysv \
        fuzz/runtime/scheduler_fuzz.c \
        libcrug/core/task.c libcrug/core/chan.c \
        libcrug/pal/linux/pal_linux.c \
        libcrug/arch/x86_64/sysv/switch.S libcrug/arch/x86_64/sysv/stack.c \
        -pthread -o "$build/scheduler_fuzz" >"$build/scheduler-build.log" 2>&1; then
    printf 'runtime-fuzz: FAIL: scheduler harness did not build\n'
    sed -n '1,40p' "$build/scheduler-build.log"
    exit 1
fi

sched_start=$(date +%s)
env ASAN_OPTIONS="$asan" UBSAN_OPTIONS="$ubsan" \
    CRUG_TASKS_WORKERS=${CRUG_TASKS_WORKERS:-2} \
    FUZZ_RUNS="$sched_runs" FUZZ_SEED=${FUZZ_SEED:-} \
    $timeout_cmd 60 "$build/scheduler_fuzz" >"$build/scheduler.log" 2>&1
sched_rc=$?
sched_elapsed=$(( $(date +%s) - sched_start ))

sched_line=$(sed -n 's/^scheduler-fuzz: sequences=.*/&/p' "$build/scheduler.log" | tail -n 1)
sched_sequences=$(printf '%s\n' "$sched_line" \
    | sed -n 's/^scheduler-fuzz: sequences=\([0-9][0-9]*\).*/\1/p')
sched_failures=$(printf '%s\n' "$sched_line" \
    | sed -n 's/.* failures=\([0-9][0-9]*\)$/\1/p')

if [ "$sched_rc" -ne 0 ] || [ -z "$sched_line" ] || [ "${sched_failures:-1}" -ne 0 ]; then
    printf 'runtime-fuzz: scheduler: FAIL rc=%d\n' "$sched_rc"
    tail -n 20 "$build/scheduler.log"
    exit 1
fi
if grep -Eq "$bad" "$build/scheduler.log"; then
    printf 'runtime-fuzz: scheduler: FAIL: sanitizer/invariant report\n'
    grep -E -A8 "$bad" "$build/scheduler.log" | head -n 40
    exit 1
fi

printf 'runtime-fuzz: scheduler: %s sequence(s), %s failure(s), 0 crash(es) [%ds]\n' \
    "${sched_sequences:-0}" "${sched_failures:-0}" "$sched_elapsed"

# ---------------------------------------------------------------------------
# Harness 2: the channel state machine under libFuzzer with ASan and UBSan.
# ---------------------------------------------------------------------------
chan_runs=${FUZZ_RUNS:-20000}
if ! clang -O1 -g -std=c11 -Wall -Wextra -Werror \
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer \
        -Ilibcrug/pal/include -Ilibcrug/core -Ilibcrug/arch/x86_64/sysv \
        fuzz/runtime/channel_fuzz.c libcrug/core/chan.c \
        libcrug/pal/fake/pal_fake.c -DCRUG_PAL_FAKE \
        -o "$build/channel_fuzz" >"$build/channel-build.log" 2>&1; then
    printf 'runtime-fuzz: FAIL: channel harness did not build\n'
    sed -n '1,40p' "$build/channel-build.log"
    exit 1
fi

chan_start=$(date +%s)
env ASAN_OPTIONS="$asan" UBSAN_OPTIONS="$ubsan" \
    $timeout_cmd 180 "$build/channel_fuzz" -runs="$chan_runs" -max_len=256 \
    -artifact_prefix="$build/" >"$build/channel.log" 2>&1
chan_rc=$?
chan_elapsed=$(( $(date +%s) - chan_start ))

chan_done=$(sed -n 's/^Done \([0-9][0-9]*\) runs.*/\1/p' "$build/channel.log" | tail -n 1)

if [ "$chan_rc" -ne 0 ] || [ -z "$chan_done" ]; then
    printf 'runtime-fuzz: channel: FAIL rc=%d\n' "$chan_rc"
    tail -n 25 "$build/channel.log"
    exit 1
fi
if grep -Eq "$bad" "$build/channel.log"; then
    printf 'runtime-fuzz: channel: FAIL: sanitizer/invariant report\n'
    grep -E -A8 "$bad" "$build/channel.log" | head -n 40
    exit 1
fi

printf 'runtime-fuzz: channel: %s run(s), 0 crash(es) [%ds]\n' \
    "$chan_done" "$chan_elapsed"

# ---------------------------------------------------------------------------
# Verdict.
# ---------------------------------------------------------------------------
printf 'runtime-fuzz: 2 harness(es), scheduler %s run(s), channel %s run(s), 0 crash(es)\n' \
    "${sched_sequences:-0}" "$chan_done"
printf 'runtime-fuzz: OK\n'
exit 0
