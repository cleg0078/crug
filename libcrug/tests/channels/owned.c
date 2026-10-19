/* owned.c - a send of an owning element moves it to the receiver.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>

#include "task.h"

static int freed;
static char text[8];

static void
producer (chan own char *out)
{
  own char *p = malloc (8);

  p[0] = 'h';
  p[1] = 'i';
  p[2] = 0;
  send (out, move (p));
}

int
main (void)
{
  chan own char *ch = channel (1);
  own char *got;

  scope {
    go producer (ch);
  }

  got = recv (ch);
  text[0] = got[0];
  text[1] = got[1];
  text[2] = 0;
  release (got);
  freed++;
  crug_task_runtime_shutdown ();

  printf ("channels-owned: text=%s releases=%d\n", text, freed);
  return (freed == 1) ? 0 : 1;
}
