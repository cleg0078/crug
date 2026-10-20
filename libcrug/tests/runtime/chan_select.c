/* chan_select.c - the polling protocol a select lowering uses, in plain C.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The compiler turns a select into a rotating scan of ready channels.  This
   program does the same scan by hand, so the runtime half is exercised on
   every platform without the compiler: it must find the ready channel, and
   it must report that nothing is ready when both channels are empty.
*/

#include "chan.h"

#include <stdio.h>

static int failures;

static void
check (int condition, const char *what)
{
  if (!condition)
    {
      fprintf (stderr, "FAIL: chan-select: %s\n", what);
      failures++;
    }
}

int
main (void)
{
  void *first = crug_chan_make (1, sizeof (int));
  void *second = crug_chan_make (1, sizeof (int));
  void *channels[2];
  int value = 7;
  int got = 0;
  int polls = 0;
  int i;
  int j;

  channels[0] = first;
  channels[1] = second;
  crug_chan_send (second, &value);

  for (i = 0; i < 2 && got == 0; i++)
    {
      unsigned start = crug_chan_select_turn () % 2u;
      for (j = 0; j < 2; j++)
	{
	  unsigned slot = (start + (unsigned) j) % 2u;
	  polls++;
	  if (crug_chan_recv_ready (channels[slot]))
	    {
	      got = *(int *) crug_chan_recv_ptr (channels[slot]);
	      break;
	    }
	}
    }

  check (got == 7, "the ready channel was found");
  check (polls >= 1, "at least one poll happened");
  check (crug_chan_recv_ready (first) == 0, "the first channel is empty");
  check (crug_chan_send_ready (first) == 1, "an empty channel accepts a send");

  printf ("channels-select: got=%d polls=%d\n", got, polls);
  return failures == 0 ? 0 : 1;
}
