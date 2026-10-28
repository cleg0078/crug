/* recv_scratch_big.c - A-01: a task's large receive buffer must not be
   retained by the task cache.

   Each task receives (allocates) a 256 KiB scratch buffer.  While the runtime
   is still running and the completed task structs sit in the task cache, the
   pre-fix task_free() kept the whole struct (including the 256 KiB buffer), so
   the in-use heap grew by roughly NTASK * BIG.  The fixed task_free() returns
   a buffer larger than CRUG_TASK_RECV_SCRATCH_CACHE_LIMIT to malloc first, so
   the completed tasks pin almost nothing.  This is a Linux-only measurement
   because it reads glibc's mallinfo2().  */
#define _DEFAULT_SOURCE 1

#include <malloc.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "task.h"
#include "task-internal.h"

#include "crug_pal.h"

#define BIG (256u * 1024u)
#define NTASK 16u

static _Atomic unsigned runs;

static void
fail (const char *what)
{
  fprintf (stderr, "FAIL: recv-scratch-big: %s\n", what);
  _Exit (1);
}

static void
big_receive (void *frame)
{
  void *scratch;

  (void) frame;
  scratch = crug_task_recv_scratch (BIG);
  if (scratch == NULL)
    fail ("no task receive buffer");
  /* Touch both ends so the allocation is really resident, not just reserved.  */
  memset (scratch, 0xab, 64u);
  ((unsigned char *) scratch)[BIG - 1u] = 0xcd;
  atomic_fetch_add_explicit (&runs, 1u, memory_order_release);
}

/* Both arena blocks and mmapped chunks: a 256 KiB buffer is above glibc's
   mmap threshold, so uordblks alone would not see it at all.  */
static size_t
in_use (void)
{
  struct mallinfo2 m = mallinfo2 ();
  return m.uordblks + m.hblkhd;
}

static _Atomic unsigned warm_done;

static void
warm_up (void *frame)
{
  (void) frame;
  if (crug_task_recv_scratch (16u) == NULL)
    fail ("no warm-up receive buffer");
  atomic_fetch_add_explicit (&warm_done, 1u, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  size_t before;
  size_t after;
  size_t held;
  unsigned i;
  unsigned spin;

  setenv ("CRUG_TASKS_WORKERS", "4", 1);
  /* Start the runtime and let its worker stacks and stack caches materialize
     before the baseline, so the measurement covers the receive buffers.  */
  crug_task_spawn (warm_up, NULL, 0);
  for (spin = 0; spin < 10000u; spin++)
    {
      crug_task_stats_get (&stats);
      if (stats.completed == 1
	  && atomic_load_explicit (&warm_done, memory_order_acquire) == 1)
	break;
      crug_pal_sleep_ns (1000000ull);
    }
  if (spin == 10000u)
    fail ("warm-up task did not complete");
  crug_pal_sleep_ns (100000000ull);
  before = in_use ();
  for (i = 0; i < NTASK; i++)
    crug_task_spawn (big_receive, NULL, 0);

  for (spin = 0; spin < 10000u; spin++)
    {
      crug_task_stats_get (&stats);
      if (stats.completed == NTASK + 1u
	  && atomic_load_explicit (&runs, memory_order_acquire) == NTASK)
	break;
      crug_pal_sleep_ns (1000000ull);
    }
  if (spin == 10000u)
    fail ("tasks did not complete");
  /* The worker releases the task struct after it observes completion; give it
     a moment to reach task_free() before measuring the cache.  */
  crug_pal_sleep_ns (200000000ull);
  after = in_use ();
  held = after > before ? after - before : 0u;

  if (crug_task_runtime_try_shutdown () != 0)
    fail ("runtime did not stop");

  printf ("recv-scratch-big: tasks=%u big=%u held=%zu limit=%u peak=%u\n",
	  NTASK, BIG, held, 64u * 1024u, stats.queue_peak);
  if (held > BIG)
    {
      fprintf (stderr,
	       "FAIL: recv-scratch-big: %zu bytes held in the task cache "
	       "(limit %u)\n", held, 64u * 1024u);
      return 1;
    }
  return 0;
}
