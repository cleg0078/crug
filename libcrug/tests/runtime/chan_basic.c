/* chan_basic.c - buffered channel round trip without any concurrency.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Part of the Phase 18 multi-OS suite: plain C against chan.h, so it runs on
   every port without the Crug compiler.
*/

#include "chan.h"

#include <stdio.h>

static int failures;

static void
check (int condition, const char *what)
{
  if (!condition)
    {
      fprintf (stderr, "FAIL: chan-basic: %s\n", what);
      failures++;
    }
}

int
main (void)
{
  void *chan = crug_chan_make (4, sizeof (int));
  struct crug_chan_stats stats;
  int values[4] = { 1, 2, 3, 4 };
  int sum = 0;
  int i;

  for (i = 0; i < 4; i++)
    crug_chan_send (chan, &values[i]);
  check (crug_chan_len (chan) == 4, "four values buffered");
  check (crug_chan_cap (chan) == 4, "capacity is four");
  check (crug_chan_recv_ready (chan) == 1, "receive ready");
  for (i = 0; i < 4; i++)
    sum += *(int *) crug_chan_recv_ptr (chan);
  check (sum == 10, "sum of the received values");
  check (crug_chan_len (chan) == 0, "empty after receiving");
  crug_chan_close (chan);
  crug_chan_close (chan);
  check (crug_chan_recv_ready (chan) == 1, "a closed channel is ready to receive");
  check (*(int *) crug_chan_recv_ptr (chan) == 0, "a closed channel yields zero");
  crug_chan_stats_get (&stats);
  check (stats.sent == 4, "sent counter");
  check (stats.received == 4, "received counter counts values, not the closed read");

  printf ("channels-basic: sent=%llu received=%llu sum=%d\n",
	  (unsigned long long) stats.sent,
	  (unsigned long long) stats.received, sum);
  return failures == 0 ? 0 : 1;
}
