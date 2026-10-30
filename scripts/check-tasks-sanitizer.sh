#!/bin/sh
# check-tasks-sanitizer.sh - the Phase 14 task runtime under ASan and UBSan.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# The context switch moves execution to a stack the sanitizers did not create,
# so this is the check that proves the switch keeps the ABI state and the
# sanitizer bookkeeping consistent: both the functional suite and the stress
# program are built with -fsanitize=address,undefined and run with the
# abort-on-error policies the Phase 14 gate names.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/libcrug-sanitizer"
mkdir -p "$build"

CC=${CC:-cc}
CFLAGS="-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
CPPFLAGS="-I$root/libcrug/pal/include -I$root/libcrug/core -I$root/libcrug/arch/x86_64/sysv"

compile () {
  out=$1
  shift
  $CC $CFLAGS $CPPFLAGS -o "$out" "$@" $root/libcrug/pal/linux/pal_linux.c \
      $root/libcrug/arch/x86_64/sysv/switch.S \
      $root/libcrug/arch/x86_64/sysv/stack.c -pthread
}

compile "$build/tasks-test" "$root/libcrug/tests/tasks_main.c" "$root/libcrug/core/task.c"
compile "$build/tasks-stress" "$root/libcrug/tests/tasks_stress.c" "$root/libcrug/core/task.c"
compile "$build/scope-stress" "$root/libcrug/tests/scope_stress.c" "$root/libcrug/core/task.c"
compile "$build/scope-free-race" "$root/libcrug/tests/runtime/scope_free_race.c" "$root/libcrug/core/task.c"

# The seam test needs a private view of the backend so it can hold a group's
# completion wake while the main thread tries to leave the group.
$CC $CFLAGS $CPPFLAGS \
    -Dcrug_pal_wake_all=crug_pal_wake_all_real \
    -Dcrug_pal_wait_u32=crug_pal_wait_u32_real \
    -c -o "$build/pal-wake-race.o" "$root/libcrug/pal/linux/pal_linux.c"
$CC $CFLAGS $CPPFLAGS -o "$build/scope-wake-race" \
    "$root/libcrug/tests/runtime/scope_wake_race.c" "$root/libcrug/core/task.c" \
    "$build/pal-wake-race.o" $root/libcrug/arch/x86_64/sysv/switch.S \
    $root/libcrug/arch/x86_64/sysv/stack.c -pthread

ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  CRUG_TASKS_WORKERS=4 "$build/tasks-test" > "$build/tasks-test.log" 2>&1
cat "$build/tasks-test.log"

ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  CRUG_TASKS_WORKERS=4 GOROUTINES_STRESS=1 "$build/tasks-stress" > "$build/tasks-stress.log" 2>&1
cat "$build/tasks-stress.log"

# The group API gets its own pass: every scope must release its state, so a
# leak shows up as both a counter and a LeakSanitizer report.
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  CRUG_TASKS_WORKERS=4 "$build/scope-stress" > "$build/scope-stress.log" 2>&1
cat "$build/scope-stress.log"

# The group lifetime: every completion wake must have returned before its
# group is released, so a group freed under a running wake shows up here.
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  CRUG_TASKS_WORKERS=4 "$build/scope-free-race" > "$build/scope-free-race.log" 2>&1
cat "$build/scope-free-race.log"

# The same lifetime under a held wake: pre-fix, leave() frees the group while
# this thread is still inside the completion wake, which ASan reports.
ASAN_OPTIONS=abort_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
  "$build/scope-wake-race" > "$build/scope-wake-race.log" 2>&1
cat "$build/scope-wake-race.log"

echo "tasks-sanitizer: ASan and UBSan clean over the suite, 100000 tasks, 200 scopes, 3000 group lifetimes and 20 held completion wakes"
