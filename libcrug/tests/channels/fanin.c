/* fanin.c - several producers feed one channel; the sum is complete.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

#define PRODUCERS 5
#define PER 4

static void
worker (chan int out)
{
  for (int i = 0; i < PER; i++)
    send (out, 1);
}

int
main (void)
{
  chan int ch = channel (PER * PRODUCERS);
  int total = 0;

  scope {
    for (int i = 0; i < PRODUCERS; i++)
      go worker (ch);
  }

  for (int i = 0; i < PRODUCERS * PER; i++)
    total += recv (ch);

  crug_task_runtime_shutdown ();
  printf ("channels-fanin: producers=%d total=%d\n", PRODUCERS, total);
  return (total == PRODUCERS * PER) ? 0 : 1;
}
