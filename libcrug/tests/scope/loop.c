/* loop.c - a break out of a scope waits for the children first.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

static int marks;

static void
mark (int i)
{
  marks += i;
}

int
main (void)
{
  int last = 0;

  for (int i = 1; i <= 4; i++)
    {
      scope {
	go mark (i);
	if (i == 2)
	  {
	    last = i;
	    break;
	  }
      }
    }

  crug_task_runtime_shutdown ();
  printf ("scope-loop: last=%d marks=%d\n", last, marks);
  return (last == 2 && marks == 3) ? 0 : 1;
}
