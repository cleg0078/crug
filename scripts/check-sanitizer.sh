#!/bin/sh
# check-sanitizer.sh - the Phase 22 sanitizer gate for the Crug runtime.
#
# Arms, in order:
#   1. scripts/check-tasks-sanitizer.sh - ASan+UBSan over the task/scope suite
#      (100000 tasks, 200 scopes) including manual fiber stack switching.
#   2. ASan+UBSan over the channel suite (libcrug/tests/runtime/*.c) and a
#      dedicated channel+PAL stress program on OS threads.
#   3. TSan over the same channel+PAL stress on OS threads only.  The v1 fiber
#      switch saves and restores stacks by hand and is not annotated with
#      TSan's fiber API, so TSan is applied only where no fiber switch happens.
#      That is the Phase 22 exception recorded in ADR 0028.
#
# The stress program is generated into build/sanitizer/ and is a build product;
# it links libcrug/core/chan.c plus the Linux PAL and stubs the three task
# symbols chan.c references, so no scheduler or fiber switch is present in the
# TSan arm.
#
# Exit status: 0 clean, 1 a sanitizer report or a failing arm, 2 a missing tool.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build="$root/build/sanitizer"
tasks_script="$root/scripts/check-tasks-sanitizer.sh"

asan_opts='abort_on_error=1:detect_leaks=1'
ubsan_opts='halt_on_error=1:print_stacktrace=1'
san_pattern='ERROR: AddressSanitizer|ERROR: LeakSanitizer|ERROR: UndefinedBehaviorSanitizer|runtime error:|SUMMARY: .*Sanitizer'
tsan_pattern='WARNING: ThreadSanitizer|ThreadSanitizer: data race|SUMMARY: ThreadSanitizer'

say () { printf '%s\n' "$*"; }
report () { say "sanitizer: FAIL: $*"; exit 1; }
tool_missing () { say "sanitizer: ERROR: $*"; exit 2; }
now () { date +%s; }
elapsed () { printf '%s' "$(( $(now) - $1 ))"; }

command -v gcc >/dev/null 2>&1 || tool_missing "gcc is not available"
command -v clang >/dev/null 2>&1 || tool_missing "clang is not available"
command -v date >/dev/null 2>&1 || tool_missing "date is not available"
[ -f "$tasks_script" ] || tool_missing "missing $tasks_script"

mkdir -p "$build" || tool_missing "cannot create $build"
start_all=$(now)

cleanup () { rm -f "$build/probe.c" "$build/probe-gcc" "$build/probe-tsan"; }
trap cleanup 0 2 15

# ---- Sanitizer runtime probes: a missing compiler or runtime is exit 2. ----
cat > "$build/probe.c" <<'PROBE_EOF'
int
main (void)
{
  return 0;
}
PROBE_EOF

if ! gcc -O1 -g -std=c11 -fsanitize=address,undefined -fno-omit-frame-pointer \
    "$build/probe.c" -o "$build/probe-gcc" -pthread >"$build/probe-gcc.log" 2>&1; then
  cat "$build/probe-gcc.log"
  tool_missing "gcc cannot build with -fsanitize=address,undefined"
fi
if ! ASAN_OPTIONS="$asan_opts" UBSAN_OPTIONS="$ubsan_opts" \
    "$build/probe-gcc" >>"$build/probe-gcc.log" 2>&1; then
  cat "$build/probe-gcc.log"
  tool_missing "the gcc ASan+UBSan runtime does not run on this host"
fi
if ! clang -O1 -g -std=c11 -fsanitize=thread -fno-omit-frame-pointer \
    "$build/probe.c" -o "$build/probe-tsan" -pthread >"$build/probe-tsan.log" 2>&1; then
  cat "$build/probe-tsan.log"
  tool_missing "clang cannot build with -fsanitize=thread"
fi
tsan_launch=''
if ! "$build/probe-tsan" >>"$build/probe-tsan.log" 2>&1; then
  # A host with 5-level paging and 32-bit mmap entropy cannot map TSan's
  # shadow unless the address space is randomised less (vm.mmap_rnd_bits) or
  # the process runs without ASLR.  When the sysctl is unavailable, retry under
  # setarch -R, which asks the kernel for ADDR_NO_RANDOMIZE before exec.
  if command -v setarch >/dev/null 2>&1 \
     && grep -Eq 'unexpected memory mapping|ADDR_NO_RANDOMIZE|CHECK failed' \
        "$build/probe-tsan.log"; then
    tsan_launch="setarch $(uname -m) -R"
    say "sanitizer: TSan needs ADDR_NO_RANDOMIZE; running the TSan arm under $tsan_launch"
    if ! $tsan_launch "$build/probe-tsan" >>"$build/probe-tsan.log" 2>&1; then
      cat "$build/probe-tsan.log"
      tool_missing "the clang TSan runtime does not run on this host"
    fi
  else
    cat "$build/probe-tsan.log"
    tool_missing "the clang TSan runtime does not run on this host"
  fi
fi

# ---- Arm 1: the existing task/scope sanitizer suite. ----
t0=$(now)
say "sanitizer: tasks arm: sh scripts/check-tasks-sanitizer.sh"
if sh "$tasks_script" > "$build/tasks.log" 2>&1; then
  cat "$build/tasks.log"
else
  rc=$?
  cat "$build/tasks.log"
  report "scripts/check-tasks-sanitizer.sh failed (exit $rc)"
fi
say "sanitizer: tasks arm clean [$(elapsed "$t0")s]"

# ---- Arm 2: ASan+UBSan over channels and the PAL (OS threads only). ----
cflags="-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
includes="-I$root/libcrug/pal/include -I$root/libcrug/core -I$root/libcrug/arch/x86_64/sysv"

cat > "$build/chan_stubs.c" <<'STUB_EOF'
/* Build-only stubs: chan_idle() yields to the scheduler when a task is
   current and a receive asks the task layer for its scratch buffer.  The
   channel arms here run without the scheduler, so there is no current task
   and the receive keeps its thread-local buffer.  */
#include <stddef.h>
#include <stdint.h>

uint32_t
crug_task_current_id (void)
{
  return 0u;
}

void
crug_task_yield (void)
{
}

void *
crug_task_recv_scratch (size_t need)
{
  (void) need;
  return NULL;
}
STUB_EOF

check_asan () {
  label=$1
  log=$2
  rc=$3
  if [ "$rc" -ne 0 ]; then
    cat "$log"
    report "ASan/UBSan arm $label failed (exit $rc)"
  fi
  if grep -Eq "$san_pattern" "$log"; then
    cat "$log"
    report "ASan/UBSan report in $label"
  fi
  say "sanitizer: $label: $(tail -n 1 "$log")"
}

run_asan () {
  label=$1
  shift
  log="$build/$label.log"
  if ASAN_OPTIONS="$asan_opts" UBSAN_OPTIONS="$ubsan_opts" "$@" >"$log" 2>&1; then
    rc=0
  else
    rc=$?
  fi
  check_asan "$label" "$log" "$rc"
}

gcc $cflags $includes \
  "$root/libcrug/tests/runtime/chan_basic.c" \
  "$root/libcrug/core/chan.c" \
  "$root/libcrug/pal/linux/pal_linux.c" \
  "$build/chan_stubs.c" -pthread -o "$build/chan_basic" \
  || report "cannot build libcrug/tests/runtime/chan_basic.c with ASan+UBSan"
run_asan chan-basic "$build/chan_basic"

gcc $cflags $includes \
  "$root/libcrug/tests/runtime/chan_select.c" \
  "$root/libcrug/core/chan.c" \
  "$root/libcrug/pal/linux/pal_linux.c" \
  "$build/chan_stubs.c" -pthread -o "$build/chan_select" \
  || report "cannot build libcrug/tests/runtime/chan_select.c with ASan+UBSan"
run_asan chan-select "$build/chan_select"

gcc $cflags $includes \
  "$root/libcrug/tests/runtime/chan_handoff.c" \
  "$root/libcrug/core/chan.c" \
  "$root/libcrug/core/task.c" \
  "$root/libcrug/pal/linux/pal_linux.c" \
  "$root/libcrug/arch/x86_64/sysv/switch.S" \
  "$root/libcrug/arch/x86_64/sysv/stack.c" -pthread -o "$build/chan_handoff" \
  || report "cannot build libcrug/tests/runtime/chan_handoff.c with ASan+UBSan"
log="$build/chan-handoff.log"
if CRUG_TASKS_WORKERS=2 ASAN_OPTIONS="$asan_opts" UBSAN_OPTIONS="$ubsan_opts" \
    "$build/chan_handoff" >"$log" 2>&1; then
  rc=0
else
  rc=$?
fi
check_asan chan-handoff "$log" "$rc"

# The dedicated channel+PAL stress program.  Four producer OS threads and the
# main thread exchange TOTAL tagged values through one capacity-4 channel;
# blocking send/recv exercise the wait paths, and the PAL thread, futex,
# clock, sleep and memory entry points are checked too.  The receiver is the
# main thread on purpose: chan.c keeps one malloc'ed receive buffer per thread
# and has no release entry point in v1, so a short-lived receiver thread would
# leave a LeakSanitizer report behind.
cat > "$build/san_stress.c" <<'STRESS_EOF'
/* san_stress.c - generated by scripts/check-sanitizer.sh (build-only).
   Channel and PAL stress on OS threads; no fiber stack switch happens, which
   is what makes this program valid under TSan (ADR 0028).  */
#include "chan.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRODUCERS 4u
#define PER_PRODUCER 200u
#define TOTAL (PRODUCERS * PER_PRODUCER)
#define TAG_BASE 1000000u

static void *the_chan;
static _Atomic uint32_t start_flag;
static _Atomic int failures;

struct producer
{
  unsigned id;
};

static struct producer producers[PRODUCERS];

static void
fail (const char *what)
{
  fprintf (stderr, "FAIL: sanitizer-stress: %s\n", what);
  atomic_fetch_add_explicit (&failures, 1, memory_order_relaxed);
}

static void
producer_main (void *arg)
{
  struct producer *p = (struct producer *) arg;
  unsigned i;

  while (atomic_load_explicit (&start_flag, memory_order_acquire) == 0u)
    crug_pal_wait_u32 (&start_flag, 0u);
  for (i = 0; i < PER_PRODUCER; i++)
    {
      uint64_t value = ((uint64_t) p->id + 1u) * TAG_BASE + i;
      crug_chan_send (the_chan, &value);
    }
}

static void
pal_checks (void)
{
  size_t page;
  uint64_t before;
  uint64_t after;

  if (crug_pal_cpu_count () < 1u)
    fail ("cpu count");
  page = crug_pal_page_size ();
  if (page < 4096u)
    fail ("page size");
  before = crug_pal_clock_monotonic_ns ();
  crug_pal_sleep_ns (1000000u);
  after = crug_pal_clock_monotonic_ns ();
  if (after < before)
    fail ("monotonic clock");
#ifndef CRUG_TSAN_ARM
  {
    unsigned char *mem = (unsigned char *) crug_pal_memory_reserve (3u * page);
    if (mem == NULL)
      fail ("memory reserve");
    else
      {
	if (!crug_pal_memory_commit (mem, 2u * page))
	  fail ("memory commit");
	mem[0] = 0x5a;
	mem[page - 1u] = 0xa5;
	if (!crug_pal_memory_protect_guard (mem + 2u * page, page))
	  fail ("guard protect");
	crug_pal_memory_release (mem, 3u * page);
      }
  }
#endif
}

int
main (void)
{
  struct crug_chan_stats stats_before;
  struct crug_chan_stats stats_after;
  crug_pal_thread *handles[PRODUCERS];
  unsigned long long count = 0;
  unsigned long long sum = 0;
  unsigned long long expected_sum = 0;
  unsigned i;

  the_chan = crug_chan_make (4, sizeof (uint64_t));
  memset (handles, 0, sizeof handles);
  atomic_store (&start_flag, 0u);

  for (i = 0; i < PRODUCERS; i++)
    {
      producers[i].id = i;
      if (crug_pal_thread_create (&handles[i], producer_main, &producers[i]) != 0)
	fail ("producer thread");
    }

  atomic_store_explicit (&start_flag, 1u, memory_order_release);
  crug_pal_wake_all (&start_flag);

  while (count < TOTAL)
    {
      uint64_t value = *(const uint64_t *) crug_chan_recv_ptr (the_chan);
      if (value == 0u)
	{
	  fail ("zero value before close");
	  break;
	}
      count++;
      sum += value;
    }

  for (i = 0; i < PRODUCERS; i++)
    if (handles[i] != NULL)
      crug_pal_thread_join (handles[i]);

  crug_chan_close (the_chan);
  crug_chan_close (the_chan);	/* idempotent */

  for (i = 0; i < PRODUCERS; i++)
    expected_sum += ((unsigned long long) (i + 1u)) * TAG_BASE * PER_PRODUCER;
  expected_sum += (unsigned long long) PRODUCERS
		  * (PER_PRODUCER * (PER_PRODUCER - 1u) / 2u);

  if (count != TOTAL)
    fail ("received count");
  if (sum != expected_sum)
    fail ("received sum");

  crug_chan_stats_get (&stats_before);
  if (stats_before.sent != TOTAL)
    fail ("sent stat");
  if (stats_before.received != TOTAL)
    fail ("received stat");
  if (stats_before.created < 1u || stats_before.live != stats_before.created)
    fail ("channel stats");
  if (crug_chan_cap (the_chan) != 4u)
    fail ("channel capacity");
  if (crug_chan_len (the_chan) != 0u)
    fail ("channel not drained");
  if (crug_chan_recv_ready (the_chan) != 1)
    fail ("closed channel not receive-ready");
  if (*(const uint64_t *) crug_chan_recv_ptr (the_chan) != 0u)
    fail ("closed sentinel not zero");
  crug_chan_stats_get (&stats_after);
  if (stats_after.received != stats_before.received)
    fail ("closed read counted as a receive");

  pal_checks ();
  crug_pal_shutdown ();

  if (atomic_load (&failures) != 0)
    return 1;
  printf ("sanitizer-stress: os-threads=%u sent=%llu received=%llu sum=%llu ok\n",
	  PRODUCERS + 1u,
	  (unsigned long long) stats_after.sent,
	  (unsigned long long) stats_after.received, sum);
  return 0;
}
STRESS_EOF

gcc $cflags $includes \
  "$build/san_stress.c" \
  "$root/libcrug/core/chan.c" \
  "$root/libcrug/pal/linux/pal_linux.c" \
  "$build/chan_stubs.c" -pthread -o "$build/san_stress" \
  || report "cannot build the channel+PAL stress program with ASan+UBSan"
run_asan san-stress "$build/san_stress"

# ---- Arm 3: TSan over the OS-thread channel arm (no fiber switch). ----
tsan_flags="-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=thread -fno-omit-frame-pointer -DCRUG_TSAN_ARM"
clang $tsan_flags $includes \
  "$build/san_stress.c" \
  "$root/libcrug/core/chan.c" \
  "$root/libcrug/pal/linux/pal_linux.c" \
  "$build/chan_stubs.c" -pthread -o "$build/san_stress_tsan" \
  || report "cannot build the OS-thread channel arm with TSan"
log="$build/tsan-os-thread-channel.log"
if TSAN_OPTIONS= $tsan_launch "$build/san_stress_tsan" >"$log" 2>&1; then
  rc=0
else
  rc=$?
fi
if [ "$rc" -ne 0 ]; then
  cat "$log"
  report "TSan arm failed (exit $rc)"
fi
if grep -Eq "$tsan_pattern" "$log"; then
  cat "$log"
  report "TSan report in the OS-thread channel arm"
fi
say "sanitizer: tsan-os-thread-channel: $(tail -n 1 "$log")"

say "sanitizer: total $(elapsed "$start_all")s"
printf '%s\n' 'sanitizer: ASan and UBSan clean over tasks/scopes/channels; TSan clean on the OS-thread channel arm; fiber exception recorded in ADR 0028'
exit 0
