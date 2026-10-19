
/* yield.c - a task gives the worker back with 'yield ();'.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int steps;

static void
worker (void)
{
  for (int i = 0; i < 4; i++)
    {
      steps++;
      yield ();
    }
}

int
main (void)
{
  struct crug_task_stats st;

  go worker ();
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&st);

  printf ("goroutines-yield: steps=%d yielded=%llu completed=%llu lost=%llu\n",
	  steps, (unsigned long long) st.yielded,
	  (unsigned long long) st.completed, (unsigned long long) st.lost);

  return (steps == 4 && st.yielded >= 4 && st.completed == 1 && st.lost == 0)
	 ? 0 : 1;
}
