/* scope_wake_race.c - a task group is not freed while its last wake runs.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   crug_task_finish() drops the last pending count and then wakes the group
   word, while crug_scope_leave() frees the group once the counters say it is
   empty.  The window between the decrement and the end of the wake therefore
   lets leave() free memory that the wake still uses (R-01).  The completion
   handshake makes the finisher raise in_flight before it updates pending and
   lower it only after the wake returned; leave() waits for both to be zero.

   The Makefile links this program with a private PAL object whose wait/wake
   entry points are renamed, so the wrapper below can hold the child's wake
   until the main thread has tried to leave.  The runtime is started OUTSIDE
   the seam first: its start wake on the epoch word has already returned, so
   every wake held after arming is a group completion wake (RT-02 - arming
   before the first spawn held the start wake instead and made the old test
   vacuous).  Pre-fix, leave() returns while the completion wake is still
   parked and the test reports early_leave=1 (with ASan the held wake reads
   freed memory); post-fix leave() cannot return before the wake completed,
   so the bounded watcher poll times out and early_leave stays 0.

   Every group gets exactly one held wake, and leave() must not have returned
   when the watcher gave up waiting: both are checked for each iteration.  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "crug_pal.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define ITERATIONS 20
#define LEAVE_WINDOW_MS 50

/* The private PAL object of this program exports these renamed definitions.  */
void crug_pal_wake_all_real (_Atomic uint32_t *addr);
void crug_pal_wait_u32_real (_Atomic uint32_t *addr, uint32_t expected);

static _Atomic int armed;          /* hold every wake_all while set          */
static _Atomic int wake_entered;   /* number of held wakes                   */
static _Atomic int wake_released;  /* number the watcher let go              */
static _Atomic int wake_completed;
static _Atomic int leave_done;     /* number of leave() calls that returned  */
static _Atomic int early_leave;
static _Atomic int child_ran;
static _Atomic int go;             /* released just before the first leave   */
static _Atomic uintptr_t last_wake_word;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

/* Hold each wake this test expects and record the word it refers to.  */
void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  if (atomic_load_explicit (&armed, memory_order_acquire) != 0)
    {
      int n = atomic_fetch_add_explicit (&wake_entered, 1,
					 memory_order_acq_rel) + 1;
      atomic_store_explicit (&last_wake_word, (uintptr_t) addr,
			     memory_order_release);
      while (atomic_load_explicit (&wake_released, memory_order_acquire) < n)
	sleep_ms (1);
      /* The PAL wake contract refers to the word even though the futex
	 syscall itself does not load it in user space (Valgrind still flags
	 the freed futex address).  Load it explicitly so ASan sees the same
	 pre-fix use after free: pre-fix the group was freed before this
	 point; post-fix leave() is still waiting for this thread.  */
      (void) atomic_load_explicit (addr, memory_order_relaxed);
      crug_pal_wake_all_real (addr);
      atomic_fetch_add_explicit (&wake_completed, 1u, memory_order_acq_rel);
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
  while (atomic_load_explicit (&go, memory_order_acquire) == 0)
    sleep_ms (1);
  atomic_fetch_add_explicit (&child_ran, 1u, memory_order_relaxed);
}

static void
noop (void *frame)
{
  (void) frame;
}

/* One iteration: the held wake is the completion wake of group i, so leave()
   must still be running while this thread holds it.  */
static void *
watcher (void *arg)
{
  int i;

  (void) arg;
  for (i = 0; i < ITERATIONS; i++)
    {
      unsigned k;
      int saw_leave = 0;

      while (atomic_load_explicit (&wake_entered, memory_order_acquire) <= i)
	sleep_ms (1);
      /* Pre-fix leave() returns while this wake is held; post-fix it must
	 keep waiting, so this bounded poll times out.  */
      for (k = 0; k < LEAVE_WINDOW_MS; k++)
	{
	  if (atomic_load_explicit (&leave_done, memory_order_acquire) > i)
	    {
	      saw_leave = 1;
	      break;
	    }
	  sleep_ms (1);
	}
      if (saw_leave)
	atomic_store_explicit (&early_leave, 1, memory_order_release);
      atomic_store_explicit (&wake_released, i + 1, memory_order_release);
      while (atomic_load_explicit (&wake_completed, memory_order_acquire) <= i)
	sleep_ms (1);
    }
  return NULL;
}

int
main (void)
{
  struct crug_task_stats stats;
  pthread_t watcher_thread;
  int status;
  int failed = 0;
  int i;

  setenv ("CRUG_TASKS_WORKERS", "2", 1);

  /* Start the runtime outside the seam: the start wake_all(&epoch) must not
     be the wake this test parks.  */
  crug_task_spawn (noop, NULL, 0);

  if (pthread_create (&watcher_thread, NULL, watcher, NULL) != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: cannot start the watcher\n");
      return 1;
    }

  atomic_store_explicit (&armed, 1, memory_order_release);
  for (i = 0; i < ITERATIONS; i++)
    {
      void *scope = crug_scope_enter ();
      atomic_store_explicit (&go, 1, memory_order_release);
      crug_task_spawn_scoped (scope, child, NULL, 0);
      crug_scope_leave (scope); /* must NOT return before the wake completed */
      atomic_store_explicit (&leave_done, i + 1, memory_order_release);
    }

  pthread_join (watcher_thread, NULL);
  atomic_store_explicit (&armed, 0, memory_order_release);

  status = crug_task_runtime_try_shutdown ();
  crug_task_stats_get (&stats);

  if (atomic_load_explicit (&early_leave, memory_order_acquire) != 0)
    {
      fprintf (stderr,
	       "FAIL: scope-wake-race: crug_scope_leave returned while the"
	       " completion wake was still in flight\n");
      failed = 1;
    }
  if (atomic_load_explicit (&child_ran, memory_order_acquire) != ITERATIONS)
    {
      fprintf (stderr, "FAIL: scope-wake-race: %d of %d children ran\n",
	       atomic_load (&child_ran), ITERATIONS);
      failed = 1;
    }
  if (atomic_load_explicit (&wake_entered, memory_order_acquire) != ITERATIONS
      || atomic_load_explicit (&wake_completed, memory_order_acquire)
	 != ITERATIONS)
    {
      fprintf (stderr,
	       "FAIL: scope-wake-race: %d wakes entered, %d completed,"
	       " expected %d (start wake must be outside the seam)\n",
	       atomic_load (&wake_entered), atomic_load (&wake_completed),
	       ITERATIONS);
      failed = 1;
    }
  if (status != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: shutdown returned %d\n",
	       status);
      failed = 1;
    }
  if (stats.completed != (unsigned long long) ITERATIONS + 1u || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: scope-wake-race: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("scope-wake-race: early_leave=%d wakes=%d completed=%d children=%d"
	  " status=%d completed_tasks=%llu lost=%llu word=%p\n",
	  atomic_load (&early_leave), atomic_load (&wake_entered),
	  atomic_load (&wake_completed), atomic_load (&child_ran), status,
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost,
	  (void *) atomic_load (&last_wake_word));
  return failed;
}
