/* cache_restart.c - task and stack caches survive a shutdown/restart cycle.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#define _POSIX_C_SOURCE 200809L

#include "task.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

static _Atomic uint32_t ran;

static void
body (void *frame)
{
  (void) frame;
  atomic_fetch_add_explicit (&ran, 1u, memory_order_relaxed);
}

int
main (void)
{
  struct crug_task_stats stats;
  uint32_t i;

#if defined(_WIN32)
  _putenv_s ("CRUG_TASKS_STACK", "16384");
  _putenv_s ("CRUG_TASKS_CACHE", "4");
  _putenv_s ("CRUG_TASKS_TASK_CACHE", "4");
#else
  setenv ("CRUG_TASKS_STACK", "16384", 0);
  setenv ("CRUG_TASKS_CACHE", "4", 0);
  setenv ("CRUG_TASKS_TASK_CACHE", "4", 0);
#endif
  for (i = 0; i < 1000u; i++)
    crug_task_spawn (body, NULL, 0);
  crug_task_runtime_shutdown ();
  for (i = 0; i < 1000u; i++)
    crug_task_spawn (body, NULL, 0);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&stats);
  printf ("cache-restart: ran=%u spawned=%llu completed=%llu lost=%llu corrupt=%llu\n",
	  atomic_load_explicit (&ran, memory_order_relaxed),
	  (unsigned long long) stats.spawned,
	  (unsigned long long) stats.completed,
	  (unsigned long long) stats.lost,
	  (unsigned long long) stats.stack_corrupt);
  return atomic_load_explicit (&ran, memory_order_relaxed) == 2000u
	 && stats.spawned == 2000u && stats.completed == 2000u
	 && stats.lost == 0 && stats.stack_corrupt == 0
	 ? 0 : 1;
}
