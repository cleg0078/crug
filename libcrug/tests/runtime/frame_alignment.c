/* frame_alignment.c - the captured frame is aligned and its tail is zeroed.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   A compiler thunk reads task->frame as the captured structure, so its offset
   must satisfy _Alignof(max_align_t); the runtime rounds the size up and zeroes
   the padding.  Zero-capture frames are valid and must still be aligned.  */

#define _DEFAULT_SOURCE 1
#include "task.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic int zero_ok;
static _Atomic int small_ok;
static _Atomic int big_ok;

static void
set_one_worker (void)
{
#ifdef _WIN32
  _putenv_s ("CRUG_TASKS_WORKERS", "1");
#else
  setenv ("CRUG_TASKS_WORKERS", "1", 1);
#endif
}

static int
aligned (void *frame)
{
  return (uintptr_t) frame % _Alignof (max_align_t) == 0;
}

static void
zero_frame (void *frame)
{
  (void) frame;
  atomic_store_explicit (&zero_ok, aligned (frame), memory_order_release);
}

static void
small_frame (void *frame)
{
  unsigned char *bytes = (unsigned char *) frame;
  int ok = aligned (frame);
  int i;

  for (i = 0; i < 13; i++)
    ok = ok && bytes[i] == (unsigned char) (i + 1);
  for (i = 13; i < 16; i++)
    ok = ok && bytes[i] == 0;
  atomic_store_explicit (&small_ok, ok, memory_order_release);
}

static void
big_frame (void *frame)
{
  unsigned char *bytes = (unsigned char *) frame;
  int ok = aligned (frame);
  int i;

  for (i = 0; i < 32; i++)
    ok = ok && bytes[i] == (unsigned char) (0x40 + i);
  atomic_store_explicit (&big_ok, ok, memory_order_release);
}

int
main (void)
{
  struct crug_task_stats stats;
  unsigned char small[13];
  unsigned char big[32];
  int failed = 0;
  int i;

  set_one_worker ();
  for (i = 0; i < 13; i++)
    small[i] = (unsigned char) (i + 1);
  for (i = 0; i < 32; i++)
    big[i] = (unsigned char) (0x40 + i);

  crug_task_spawn (zero_frame, NULL, 0);
  crug_task_spawn (small_frame, small, sizeof small);
  crug_task_spawn (big_frame, big, sizeof big);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);

  if (!atomic_load_explicit (&zero_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: frame-alignment: the zero frame is misaligned\n");
      failed = 1;
    }
  if (!atomic_load_explicit (&small_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: frame-alignment: the 13-byte frame is misaligned,"
	       " truncated or its padding is not zero\n");
      failed = 1;
    }
  if (!atomic_load_explicit (&big_ok, memory_order_acquire))
    {
      fprintf (stderr, "FAIL: frame-alignment: the 32-byte frame is misaligned"
	       " or damaged\n");
      failed = 1;
    }
  if (stats.completed != 3 || stats.lost != 0)
    {
      fprintf (stderr, "FAIL: frame-alignment: completed=%llu lost=%llu\n",
	       (unsigned long long) stats.completed,
	       (unsigned long long) stats.lost);
      failed = 1;
    }

  printf ("frame-alignment: completed=%llu align=%zu\n",
	  (unsigned long long) stats.completed, _Alignof (max_align_t));
  return failed;
}
