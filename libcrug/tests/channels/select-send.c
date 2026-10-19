/* select-send.c - a send arm is taken when the channel has room.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

#include "task.h"

int
main (void)
{
  chan int ch = channel (1);
  int sent = 0;
  int got = 0;

  /* Capacity 1 is empty, so the send arm is ready; afterwards the channel is
     full and the default is taken until the value is received.  */
  select {
    case send (ch, 111):
      sent++;
    default:
      sent += 100;
  }

  got = recv (ch);
  crug_task_runtime_shutdown ();

  printf ("channels-select-send: sent=%d got=%d\n", sent, got);
  return (sent == 1 && got == 111) ? 0 : 1;
}
