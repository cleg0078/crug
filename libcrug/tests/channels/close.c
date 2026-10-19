/* close.c - close is idempotent and a receive after close yields NULL.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

int
main (void)
{
  chan int ch = channel (2);

  send (ch, 5);
  send (ch, 6);
  close (ch);
  close (ch);

  int a = recv (ch);
  int b = recv (ch);
  int tail = recv (ch);
  crug_task_runtime_shutdown ();

  printf ("channels-close: a=%d b=%d tail=%d\n", a, b, tail);
  return (a == 5 && b == 6 && tail == 0) ? 0 : 1;
}
