/* scope_wake_race.c - a task group is not freed while its last wake runs.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   crug_task_finish() drops the last pending count and then wakes the group
   word, while crug_scope_leave() frees the group as soon as pending reaches
   zero.  The window between the decrement and the end of the wake therefore
   let leave() free memory that the wake still used (R-01).  The handshake fix
   makes the finisher publish an in-flight count before it updates pending and
   drop it only after the wake returned; leave() waits for both to be zero.

   The Makefile links this program with a private PAL object whose wait/wake
   entry points are renamed, so the wrappers here can hold the child's wake
   until the main thread has tried to leave.  Pre-fix, leave() returns while
   the wake is still parked and the test reports early_leave=1 (with ASan the
   held wake reads the freed word and reports a heap-use-after-free).  With
   the handshake, leave() cannot return before the wake completed, so the
   watcher observes a timeout and early_leave=0.  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* The private PAL object of this program exports these renamed definitions.  */
void crug_pal_wake_all_real (_Atomic uint32_t *addr);
void crug_pal_wait_u32_real (_Atomic uint32_t *addr, uint32_t expected);

static _Atomic int in_scope_wake;
static _Atomic int wake_entered;
static _Atomic int wake_released;
static _Atomic int wake_completed;
static _Atomic int leave_done;
static _Atomic int early_leave;
static _Atomic int child_ran;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

/* Hold the one wake this test expects: the last child's completion wake.  */
void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  if (atomic_load_explicit (&in_scope_wake, memory_order_acquire) != 0)
    {
      atomic_store_explicit (&wake_entered, 1, memory_order_release);
      while (atomic_load_explicit (&wake_released, memory_order_acquire) == 0)
	sleep_ms (1);
      /* The PAL wake contract refers to the word even though the futex
	 syscall itself does not load it in user space (Valgrind still flags
	 the freed futex address).  Load it explicitly so ASan sees the same
	 pre-fix use after free: pre-fix the group was freed before this
	 point; post-fix leave() is still waiting for this thread.  */
      (void) atomic_load_explicit (addr, memory_order_relaxed);
      crug_pal_wake_all_real (addr);
      atomic_store_explicit (&wake_completed, 1, memory_order_release);
    }
  else
    crug_pal_wake_all_real (addr);
}

/* Waiting in the PAL is only an optimisation: leave() re-reads its predicate
   after the call and also polls the in-flight count, so an immediate return
   is a permitted spurious wake-up.  A short sleep keeps the main thread from
   spinning without yielding.  */
void
crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected)
{
  (void) addr;
  (void) expected;
  sleep_ms (1);
}

static void
child (void *frame)
{
  (void) frame;
  atomic_store_explicit (&child_ran, 1, memory_order_release);
}

static void
watcher (void *arg)
{
  unsigned i;
  int saw_leave = 0;

  (void) arg;
  while (atomic_load_explicit (&wake_entered, memory_order_acquire) == 0)
    sleep_ms (1);

  /* Pre-fix leave() returns while this wake is still held; post-fix it must
     keep waiting, so this bounded loop times out.  */
  for (i = 0; i < 300u; i++)
    {
      if (atomic_load_explicit (&leave_done, memory_order_acquire) != 0)
	{
	  saw_leave = 1;
	  break;
	}
      sleep_ms (1);
    }

  atomic_store_explicit (&early_leave, saw_leave, memory_order_release);
  atomic_store_explicit (&wake_released, 1, memory_order_release);
  while (atomic_load_explicit (&wake_completed, memory_order_acquire) == 0)
    sleep_ms (1);
}

int
main (void)
{
  struct crug_scope *scope;
  struct crug_task_stats stats;
  crug_pal_thread *watcher_thread = NULL;
  int status;
  int failed = 0;

  setenv ("CRUG_TASKS_WORKERS", "1", 1);

  if (crug_pal_thread_create (&watcher_thread, watcher, NULL) != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: cannot start the watcher\n");
      return 1;
    }

  scope = crug_scope_enter ();
  atomic_store_explicit (&in_scope_wake, 1, memory_order_release);
  crug_task_spawn_scoped (scope, child, NULL, 0);
  crug_scope_leave (scope);
  atomic_store_explicit (&leave_done, 1, memory_order_release);

  if (crug_pal_thread_join (watcher_thread) != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: cannot join the watcher\n");
      return 1;
    }
  atomic_store_explicit (&in_scope_wake, 0, memory_order_release);

  status = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&stats);

  if (atomic_load_explicit (&early_leave, memory_order_acquire) != 0)
    {
      fprintf (stderr,
	       "FAIL: scope-wake-race: crug_scope_leave returned while the"
	       " completion wake was still in flight\n");
      failed = 1;
    }
  if (atomic_load_explicit (&child_ran, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: scope-wake-race: the child did not run\n");
      failed = 1;
    }
  if (atomic_load_explicit (&wake_completed, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: scope-wake-race: the completion wake did not run\n");
      failed = 1;
    }
  if (status != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: shutdown returned %d\n", status);
      failed = 1;
    }
  if (stats.completed != 1 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("scope-wake-race: early_leave=%d wake_entered=%d wake_completed=%d"
	  " child_ran=%d status=%d completed=%llu lost=%llu\n",
	  atomic_load_explicit (&early_leave, memory_order_acquire),
	  atomic_load_explicit (&wake_entered, memory_order_acquire),
	  atomic_load_explicit (&wake_completed, memory_order_acquire),
	  atomic_load_explicit (&child_ran, memory_order_acquire),
	  status, (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost);
  return failed;
}
