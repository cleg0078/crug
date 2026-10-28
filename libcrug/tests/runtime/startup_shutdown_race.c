/* startup_shutdown_race.c - a shutdown cannot join an unpublished worker.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   S-01: runtime_start() published started/worker_count under the queue lock
   and then created the workers outside it, so a concurrent
   crug_task_runtime_try_shutdown() could own the stop, read worker_count,
   join threads[0] while the handle was still NULL and report 3 with
   stop_failed set - leaving the runtime half started.

   The Makefile links this program with a private PAL object whose thread
   create/join entry points are renamed, so the wrappers here can park the
   starter inside the first worker creation.  Pre-fix the shutdown caller
   finishes while the starter is parked (status=3, null_join=1); post-fix it
   must wait for the start to publish every handle, so the release produces a
   clean stop (status=0, null_join=0).  The pre-fix branch reports and leaves
   without releasing the starter: its parked spawn would otherwise abort with
   crug-CONC-029 and the destructor would wait out the 60 s overlap timeout.
   */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int crug_pal_thread_create_real (crug_pal_thread **out,
				 void (*entry) (void *), void *arg);
int crug_pal_thread_join_real (crug_pal_thread *thread);

static _Atomic int arm_create_block;
static _Atomic int create_blocked;
static _Atomic int release_create;
static _Atomic int null_join;
static _Atomic int spawn_returned;
static _Atomic int shutdown_entered;
static _Atomic int shutdown_done;
static _Atomic int task_ran;
static int shutdown_status;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

int
crug_pal_thread_create (crug_pal_thread **out, void (*entry) (void *),
			void *arg)
{
  if (atomic_exchange_explicit (&arm_create_block, 0,
				memory_order_acq_rel) != 0)
    {
      atomic_store_explicit (&create_blocked, 1, memory_order_release);
      while (atomic_load_explicit (&release_create, memory_order_acquire) == 0)
	sleep_ms (1);
    }
  return crug_pal_thread_create_real (out, entry, arg);
}

int
crug_pal_thread_join (crug_pal_thread *thread)
{
  if (thread == NULL)
    atomic_store_explicit (&null_join, 1, memory_order_release);
  return crug_pal_thread_join_real (thread);
}

static void
mark (void *frame)
{
  (void) frame;
  atomic_store_explicit (&task_ran, 1, memory_order_release);
}

static void
starter (void *arg)
{
  (void) arg;
  /* Arm the block only now: the test's own create of this thread must pass. */
  atomic_store_explicit (&arm_create_block, 1, memory_order_release);
  crug_task_spawn (mark, NULL, 0);
  atomic_store_explicit (&spawn_returned, 1, memory_order_release);
}

static void
stopper (void *arg)
{
  (void) arg;
  atomic_store_explicit (&shutdown_entered, 1, memory_order_release);
  shutdown_status = crug_task_runtime_try_shutdown ();
  atomic_store_explicit (&shutdown_done, 1, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  crug_pal_thread *start_thread = NULL;
  crug_pal_thread *stop_thread = NULL;
  unsigned i;
  int failed = 0;

  setenv ("CRUG_TASKS_WORKERS", "1", 1);

  if (crug_pal_thread_create (&start_thread, starter, NULL) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-race: cannot start the"
	       " starter\n");
      return 1;
    }
  for (i = 0; i < 2000u && atomic_load_explicit (&create_blocked,
						 memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (atomic_load_explicit (&create_blocked, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-race: the first worker create"
	       " was never reached\n");
      return 1;
    }

  if (crug_pal_thread_create (&stop_thread, stopper, NULL) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-race: cannot start the"
	       " shutdown caller\n");
      return 1;
    }
  for (i = 0; i < 2000u
	      && atomic_load_explicit (&shutdown_entered,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (atomic_load_explicit (&shutdown_entered, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-race: the shutdown caller never"
	       " ran\n");
      return 1;
    }
  for (i = 0; i < 300u
	      && atomic_load_explicit (&shutdown_done,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);

  if (atomic_load_explicit (&shutdown_done, memory_order_acquire) == 0)
    {
      /* The fix: the stop waits for the start to reach "started".  */
      atomic_store_explicit (&release_create, 1, memory_order_release);
      if (crug_pal_thread_join (start_thread) != 0)
	{
	  fprintf (stderr, "FAIL: startup-shutdown-race: cannot join the"
		   " starter\n");
	  return 1;
	}
      for (i = 0; i < 5000u
		  && atomic_load_explicit (&shutdown_done,
					   memory_order_acquire) == 0; i++)
	sleep_ms (1);
      if (crug_pal_thread_join (stop_thread) != 0)
	{
	  fprintf (stderr, "FAIL: startup-shutdown-race: cannot join the"
		   " shutdown caller\n");
	  return 1;
	}
      crug_task_stats_get (&stats);

      if (shutdown_status != 0)
	{
	  fprintf (stderr,
		   "FAIL: startup-shutdown-race: shutdown returned %d\n",
		   shutdown_status);
	  failed = 1;
	}
      if (atomic_load_explicit (&null_join, memory_order_acquire) != 0)
	{
	  fprintf (stderr, "FAIL: startup-shutdown-race: a worker handle was"
		   " joined before it was published\n");
	  failed = 1;
	}
      if (atomic_load_explicit (&spawn_returned, memory_order_acquire) != 1
	  || atomic_load_explicit (&task_ran, memory_order_acquire) != 1)
	{
	  fprintf (stderr, "FAIL: startup-shutdown-race: the starting spawn was"
		   " not admitted (returned=%d ran=%d)\n",
		   atomic_load_explicit (&spawn_returned, memory_order_acquire),
		   atomic_load_explicit (&task_ran, memory_order_acquire));
	  failed = 1;
	}
      if (stats.completed != 1 || stats.lost != 0)
	{
	  fprintf (stderr, "FAIL: startup-shutdown-race: completed=%llu"
		   " lost=%llu\n",
		   (unsigned long long) stats.completed,
		   (unsigned long long) stats.lost);
	  failed = 1;
	}

      printf ("startup-shutdown-race: blocked_create=1 status=%d null_join=%d"
	      " returned=%d ran=%d completed=%llu lost=%llu\n",
	      shutdown_status,
	      atomic_load_explicit (&null_join, memory_order_acquire),
	      atomic_load_explicit (&spawn_returned, memory_order_acquire),
	      atomic_load_explicit (&task_ran, memory_order_acquire),
	      (unsigned long long) stats.completed,
	      (unsigned long long) stats.lost);
      return failed;
    }

  /* Pre-fix: the stop owned the shutdown while the worker handle was still
     unpublished and joined NULL.  Leave the starter parked: nothing can be
     run after this point (the destructor would sleep 60 s and the parked
     spawn would abort with crug-CONC-029).  */
  printf ("startup-shutdown-race: blocked_create=1 status=%d null_join=%d\n",
	  shutdown_status,
	  atomic_load_explicit (&null_join, memory_order_acquire));
  fflush (stdout);
  _Exit (1);
}
