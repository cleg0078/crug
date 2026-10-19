
/* shutdown.c - the shutdown completes every task and the runtime restarts.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int marks[5];

static void
mark (int i)
{
  marks[i] = 1;
}

int
main (void)
{
  struct crug_task_stats st;
  int seen = 0;

  for (int i = 0; i < 3; i++)
    go mark (i);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&st);

  for (int i = 0; i < 3; i++)
    seen += marks[i];

  if (seen != 3 || st.completed != 3 || st.lost != 0)
    {
      printf ("goroutines-shutdown: first batch seen=%d completed=%llu\n",
	      seen, (unsigned long long) st.completed);
      return 1;
    }

  /* The shutdown is idempotent and a later spawn starts the runtime again.  */
  crug_task_runtime_shutdown ();
  go mark (3);
  go mark (4);
  crug_task_runtime_shutdown ();
  crug_task_stats_get (&st);

  seen = 0;
  for (int i = 0; i < 5; i++)
    seen += marks[i];

  printf ("goroutines-shutdown: seen=%d completed=%llu lost=%llu\n",
	  seen, (unsigned long long) st.completed, (unsigned long long) st.lost);

  /* spawned and completed are lifetime counters, so five tasks in total.  */
  return (seen == 5 && st.completed == 5 && st.lost == 0) ? 0 : 1;
}
