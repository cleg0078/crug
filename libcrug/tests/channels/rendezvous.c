/* rendezvous.c - capacity 0 is a synchronous handoff between two tasks.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int total;

static void
producer (chan int ch)
{
  for (int i = 0; i < 3; i++)
    send (ch, i + 1);
}

static void
consumer (chan int ch)
{
  for (int i = 0; i < 3; i++)
    total += recv (ch);
}

int
main (void)
{
  /* The channel holds nothing, so each send waits for the matching receive:
     three handoffs and no buffer.  The scope waits for both children before
     the total is read (ADR 0024).  */
  chan int ch = channel (0);

  scope {
    go producer (ch);
    go consumer (ch);
  }

  crug_task_runtime_shutdown ();
  printf ("channels-rendezvous: handoffs=%d total=%d\n", 3, total);
  return (total == 6) ? 0 : 1;
}
