
/* spawn.c - spawn tasks from Crug 'go' statements and check completion.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int values[8];

static void
store (int i, int v)
{
  values[i] = v;
}

int
main (void)
{
  struct crug_task_stats st;
  int sum = 0;

  for (int i = 0; i < 8; i++)
    go store (i, i + 1);

  crug_task_runtime_shutdown ();
  crug_task_stats_get (&st);

  for (int i = 0; i < 8; i++)
    sum += values[i];

  printf ("goroutines-spawn: sum=%d spawned=%llu completed=%llu lost=%llu\n",
	  sum, (unsigned long long) st.spawned,
	  (unsigned long long) st.completed, (unsigned long long) st.lost);

  return (sum == 36 && st.spawned == 8 && st.completed == 8 && st.lost == 0)
	 ? 0 : 1;
}
