/* startup_shutdown_handshake.c - the start hook opens the S-01 window.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The runtime calls the weak hooks crug_task_start_hook() after it published
   "started" and before it creates any worker, and crug_task_join_hook()
   before each join.  Overriding the start hook parks the starter in that
   window while another thread calls crug_task_runtime_try_shutdown(): the
   shutdown must not return (and must not join a NULL handle) while the start
   is still in flight, and the spawn that triggered the start must still be
   admitted before the stop proceeds.  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "task-internal.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int start_entered;
static _Atomic int release_start;
static _Atomic int join_count;
static _Atomic int null_join;
static _Atomic int spawn_returned;
static _Atomic int shutdown_entered;
static _Atomic int shutdown_done;
static _Atomic int early_stop;
static _Atomic int task_ran;
static int shutdown_status;

static void
sleep_ms (unsigned ms)
{
  crug_pal_sleep_ns ((uint64_t) ms * 1000000u);
}

void
crug_task_start_hook (void)
{
  atomic_store_explicit (&start_entered, 1, memory_order_release);
  while (atomic_load_explicit (&release_start, memory_order_acquire) == 0)
    sleep_ms (1);
}

void
crug_task_join_hook (unsigned index, void *handle)
{
  (void) index;
  if (handle == NULL)
    atomic_store_explicit (&null_join, 1, memory_order_release);
  atomic_fetch_add_explicit (&join_count, 1u, memory_order_relaxed);
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
      fprintf (stderr, "FAIL: startup-shutdown-handshake: cannot start the"
	       " starter\n");
      return 1;
    }
  for (i = 0; i < 2000u
	      && atomic_load_explicit (&start_entered,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (atomic_load_explicit (&start_entered, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: the start hook never"
	       " ran\n");
      return 1;
    }

  if (crug_pal_thread_create (&stop_thread, stopper, NULL) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: cannot start the"
	       " shutdown caller\n");
      return 1;
    }
  for (i = 0; i < 2000u
	      && atomic_load_explicit (&shutdown_entered,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (atomic_load_explicit (&shutdown_entered, memory_order_acquire) == 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: the shutdown caller"
	       " never ran\n");
      return 1;
    }

  /* While start_state is "starting", a stop must not complete.  */
  sleep_ms (100);
  if (atomic_load_explicit (&shutdown_done, memory_order_acquire) != 0)
    atomic_store_explicit (&early_stop, 1, memory_order_release);

  atomic_store_explicit (&release_start, 1, memory_order_release);
  if (crug_pal_thread_join (start_thread) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: cannot join the"
	       " starter\n");
      return 1;
    }
  for (i = 0; i < 5000u
	      && atomic_load_explicit (&shutdown_done,
				       memory_order_acquire) == 0; i++)
    sleep_ms (1);
  if (crug_pal_thread_join (stop_thread) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: cannot join the"
	       " shutdown caller\n");
      return 1;
    }
  crug_task_stats_get (&stats);

  if (atomic_load_explicit (&early_stop, memory_order_acquire) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: the shutdown"
	       " completed while the start was still in flight\n");
      failed = 1;
    }
  if (shutdown_status != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: shutdown returned"
	       " %d\n", shutdown_status);
      failed = 1;
    }
  if (atomic_load_explicit (&null_join, memory_order_acquire) != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: a NULL worker handle"
	       " was joined\n");
      failed = 1;
    }
  if (atomic_load_explicit (&join_count, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: joined %u workers"
	       " instead of 1\n",
	       atomic_load_explicit (&join_count, memory_order_acquire));
      failed = 1;
    }
  if (atomic_load_explicit (&spawn_returned, memory_order_acquire) != 1
      || atomic_load_explicit (&task_ran, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: the starting spawn"
	       " was not admitted (returned=%d ran=%d)\n",
	       atomic_load_explicit (&spawn_returned, memory_order_acquire),
	       atomic_load_explicit (&task_ran, memory_order_acquire));
      failed = 1;
    }
  if (stats.completed != 1 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: startup-shutdown-handshake: completed=%llu"
	       " lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("startup-shutdown-handshake: status=%d null_join=%d joins=%u"
	  " early_stop=%d returned=%d ran=%d completed=%llu lost=%llu\n",
	  shutdown_status,
	  atomic_load_explicit (&null_join, memory_order_acquire),
	  atomic_load_explicit (&join_count, memory_order_acquire),
	  atomic_load_explicit (&early_stop, memory_order_acquire),
	  atomic_load_explicit (&spawn_returned, memory_order_acquire),
	  atomic_load_explicit (&task_ran, memory_order_acquire),
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost);
  return failed;
}
