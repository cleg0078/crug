/* select.c - a select picks the arm whose channel is ready.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static void
feed (chan int ch, int base)
{
  for (int i = 0; i < 3; i++)
    send (ch, base + i);
}

int
main (void)
{
  chan int a = channel (3);
  chan int b = channel (3);
  int from_a = 0;
  int from_b = 0;
  int defaults = 0;

  scope {
    go feed (a, 10);
    go feed (b, 20);
  }

  close (a);
  close (b);

  /* Both channels are closed, so every pass has a ready arm: the default is
     never taken and the rotation shares the arms.  */
  for (int i = 0; i < 6; i++)
    {
      select {
	case v = recv (a):
	  from_a += v;
	case w = recv (b):
	  from_b += w;
	default:
	  defaults++;
      }
    }

  crug_task_runtime_shutdown ();
  printf ("channels-select: a=%d b=%d defaults=%d\n",
	  from_a, from_b, defaults);
  return (from_a == 33 && from_b == 63 && defaults == 0) ? 0 : 1;
}
