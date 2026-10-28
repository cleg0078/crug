/* linux_spawn_shutdown.c - a spawn after the stop claim still aborts.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   K-013/K-014: once a shutdown has claimed the runtime, a new spawn must
   still die with crug-CONC-029 ("cannot spawn during shutdown").  The S-01
   fix only moves the stop claim so it cannot beat the starter's own
   admission; it must not weaken this admission check.

   crug_task_join_hook() parks the shutdown caller after it claimed stopping
   and before it joins the worker, so the main thread can spawn into that
   window deterministically.  The expected outcome is the fatal diagnostic and
   a non-zero exit, which is why the test is registered as an abort program
   for the Linux backend (check-RUNTIME looks for the message because the
   target name matches *spawn-shutdown).  If the spawn is admitted instead,
   the test reports the regression and exits without the destructor (the
   shutdown caller is parked in the join hook).  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "task-internal.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int join_hook_entered;
static _Atomic int shutdown_entered;
static _Atomic int second_ran;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

void
crug_task_join_hook (unsigned index, void *handle)
{
  (void) index;
  (void) handle;
  atomic_store_explicit (&join_hook_entered, 1, memory_order_release);
  /* Hold the owner between the stop claim and the join.  The process is
     expected to abort before this is ever released.  */
  for (;;)
    sleep_ms (1);
}

static void
mark (void *frame)
{
  (void) frame;
}

static void
second (void *frame)
{
  (void) frame;
  atomic_store_explicit (&second_ran, 1, memory_order_release);
}

static void
stopper (void *arg)
{
  (void) arg;
  atomic_store_explicit (&shutdown_entered, 1, memory_order_release);
  (void) crug_task_runtime_try_shutdown ();
}

int
main (void)
{
  crug_pal_thread *stop_thread = NULL;
  unsigned i;

  setenv ("CRUG_TASKS_WORKERS", "1", 1);

  crug_task_spawn (mark, NULL, 0);
  if (crug_pal_thread_create (&stop_thread, stopper, NULL) != 0)
    {
      fprintf (stderr, "FAIL: linux-spawn-shutdown: cannot start the shutdown"
	       " caller\n");
      return 1;
    }
  for (i = 0; i < 2000u
	      && atomic_load_explicit (&join_hook_entered,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (atomic_load_explicit (&join_hook_entered, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: linux-spawn-shutdown: the join hook never ran\n");
      return 1;
    }

  /* stopping is claimed: this spawn must abort with crug-CONC-029.  */
  crug_task_spawn (second, NULL, 0);

  fprintf (stderr, "FAIL: linux-spawn-shutdown: a spawn after the stop claim"
	   " was admitted (ran=%d)\n",
	   atomic_load_explicit (&second_ran, memory_order_acquire));
  fflush (stderr);
  /* The shutdown caller is parked in the join hook: skip the destructor.  */
  _Exit (1);
}
