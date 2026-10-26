/* recv_scratch.c - two tasks on one worker never share a receive buffer.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The first task keeps the pointer returned by crug_chan_recv_ptr(), yields,
   and reads it again.  The second task on the same worker receives another
   value in between.  A thread-local shared buffer would let the second receive
   overwrite the memory the first task is still reading; the per-task scratch
   buffer must keep the first value intact.  The main thread keeps using the
   per-thread fallback, which this test also checks.  */

#define _DEFAULT_SOURCE 1
#include "chan.h"
#include "task.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void *handoff;
static void *warm;
static _Atomic int first_ok;
static _Atomic int first_value;
static _Atomic int second_ok;
static _Atomic int main_ok;

static void
set_one_worker (void)
{
#ifdef _WIN32
  _putenv_s ("CRUG_TASKS_WORKERS", "1");
#else
  setenv ("CRUG_TASKS_WORKERS", "1", 1);
#endif
}

static void
first (void *frame)
{
  unsigned char *value;
  (void) frame;
  value = (unsigned char *) crug_chan_recv_ptr (handoff);
  atomic_store_explicit (&first_value, value[0], memory_order_release);
  crug_task_yield ();
  atomic_store_explicit (&first_ok, value[0] == 0x11, memory_order_release);
}

static void
second (void *frame)
{
  unsigned char value = 0x11;
  unsigned char *reused;
  (void) frame;
  crug_chan_send (handoff, &value);
  reused = (unsigned char *) crug_chan_recv_ptr (warm);
  atomic_store_explicit (&second_ok, reused[0] == 0x22, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  void *main_buffer;
  unsigned char sent = 0x22;
  unsigned char main_value = 0x33;
  unsigned char *pointer;
  int ok = 1;

  set_one_worker ();
  handoff = crug_chan_make (0, 1);
  warm = crug_chan_make (1, 1);

  /* The second task gets this value without blocking.  */
  crug_chan_send (warm, &sent);

  /* The main thread has no task scratch, so it keeps a per-thread buffer.  */
  main_buffer = crug_chan_make (1, 1);
  crug_chan_send (main_buffer, &main_value);
  pointer = (unsigned char *) crug_chan_recv_ptr (main_buffer);
  atomic_store_explicit (&main_ok, pointer[0] == 0x33, memory_order_release);

  crug_task_spawn (first, NULL, 0);
  crug_task_spawn (second, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  if (!atomic_load_explicit (&main_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: recv-scratch: the main thread lost its value\n");
      ok = 0;
    }
  if (atomic_load_explicit (&first_value, memory_order_acquire) != 0x11)
    {
      fprintf (stderr, "FAIL: recv-scratch: the first task got %d\n",
	       atomic_load_explicit (&first_value, memory_order_acquire));
      ok = 0;
    }
  if (!atomic_load_explicit (&first_ok, memory_order_acquire))
    {
      fprintf (stderr,
	       "FAIL: recv-scratch: the second task overwrote the first"
	       " task's receive buffer\n");
      ok = 0;
    }
  if (!atomic_load_explicit (&second_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: recv-scratch: the second task lost its value\n");
      ok = 0;
    }
  if (stats.completed != 2 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: recv-scratch: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      ok = 0;
    }

  printf ("recv-scratch: completed=%llu first=%d main=ok\n",
	  (unsigned long long) stats.completed,
	  atomic_load_explicit (&first_value, memory_order_acquire));
  return ok ? 0 : 1;
}
