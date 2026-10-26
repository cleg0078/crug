/* frame_overflow.c - an overflowing frame size is a fatal CONC-028.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The driver runs this program expecting a non-zero exit and the runtime's
   crug-CONC-028 diagnostic: a wrapped size computation must never reserve a
   small block for a huge captured frame.  */

#include "task.h"

#include <stdint.h>
#include <stdio.h>

static void
never (void *frame)
{
  (void) frame;
}

int
main (void)
{
  fflush (stdout);
  crug_task_spawn (never, NULL, SIZE_MAX);
  printf ("frame-overflow: the spawn returned\n");
  return 0;
}
