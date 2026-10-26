/* shutdown_concurrent.c - two threads shutting down at once.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   One caller owns the shutdown; the other waits for it and then reports that
   the runtime is stopped.  Status 2 and 3 are never produced by a valid
   overlap, and the task must have completed before the owner returns.  This
   test needs real PAL threads, so the Makefile builds it for the Linux
   backend only.  */

#define _DEFAULT_SOURCE 1
#include "task.h"
#include "crug_pal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int ran;
static int statuses[2];

static void
set_one_worker (void)
{
  setenv ("CRUG_TASKS_WORKERS", "1", 1);
}

static void
mark (void *frame)
{
  (void) frame;
  atomic_store_explicit (&ran, 1, memory_order_release);
}

static void
shutdown_one (void *arg)
{
  statuses[*(int *) arg] = crug_task_runtime_try_shutdown ();
}

int
main (void)
{
  struct crug_task_stats stats;
  crug_pal_thread *threads[2];
  int indices[2] = { 0, 1 };
  int i;
  int zeros = 0;
  int failed = 0;

  set_one_worker ();
  crug_task_spawn (mark, NULL, 0);

  for (i = 0; i < 2; i++)
    {
      if (crug_pal_thread_create (&threads[i], shutdown_one, &indices[i]) != 0)
	{
	  fprintf (stderr, "FAIL: shutdown-concurrent: cannot start a thread\n");
	  return 1;
	}
    }
  for (i = 0; i < 2; i++)
    {
      if (crug_pal_thread_join (threads[i]) != 0)
	{
	  fprintf (stderr, "FAIL: shutdown-concurrent: cannot join a thread\n");
	  return 1;
	}
    }

  crug_task_stats_get (&stats);
  for (i = 0; i < 2; i++)
    {
      if (statuses[i] == 0)
	zeros++;
      else if (statuses[i] != 1)
	{
	  fprintf (stderr,
		   "FAIL: shutdown-concurrent: status %d is not 0 or 1\n",
		   statuses[i]);
	  failed = 1;
	}
    }
  if (zeros != 1)
    {
      fprintf (stderr,
	       "FAIL: shutdown-concurrent: %d owners instead of one\n", zeros);
      failed = 1;
    }
  if (atomic_load_explicit (&ran, memory_order_acquire) != 1)
    {
      fprintf (stderr, "FAIL: shutdown-concurrent: the task did not run\n");
      failed = 1;
    }
  if (stats.completed != 1 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: shutdown-concurrent: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  /* After the overlap the runtime is stopped and can start again.  */
  crug_task_spawn (mark, NULL, 0);
  if (crug_task_runtime_try_shutdown () != 0)
    {
      fprintf (stderr, "FAIL: shutdown-concurrent: the restart did not stop"
	       " with 0\n");
      failed = 1;
    }

  printf ("shutdown-concurrent: statuses=%d,%d completed=%llu\n",
	  statuses[0], statuses[1], (unsigned long long) stats.completed);
  return failed;
}
