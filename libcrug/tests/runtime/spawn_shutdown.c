/* spawn_shutdown.c - a spawn during shutdown is a fatal diagnostic.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The fake backend runs the queued task while the owner is joining, so the
   spawn races a real stopping state deterministically.  The driver expects a
   non-zero exit and the "cannot spawn during shutdown" message.  */

#include "task.h"

#include <stdatomic.h>
#include <stdio.h>

static void
inner (void *frame)
{
  int status;
  (void) frame;
  status = crug_task_runtime_try_shutdown ();
  fprintf (stderr, "spawn-shutdown: inner try_shutdown=%d\n", status);
  fflush (stderr);
  if (status != 2)
    {
      fprintf (stderr, "FAIL: spawn-shutdown: the inner task got status %d\n",
	       status);
      return;
    }
  crug_task_spawn (inner, NULL, 0);
  fprintf (stderr, "FAIL: spawn-shutdown: the spawn returned\n");
}

int
main (void)
{
  crug_task_spawn (inner, NULL, 0);
  crug_task_runtime_shutdown ();
  printf ("spawn-shutdown: the shutdown returned\n");
  return 0;
}
