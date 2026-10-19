/* roundtrip.c - a buffered channel carries values in order.

   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>

int
main (void)
{
  chan int ch = channel (4);
  int sum = 0;

  for (int i = 1; i <= 4; i++)
    send (ch, i);

  for (int i = 0; i < 4; i++)
    sum += recv (ch);

  /* A closed channel still drains, then yields the zeroed sentinel.  */
  close (ch);
  close (ch);
  int last = recv (ch);

  printf ("channels-roundtrip: values=4 sum=%d closed=%d\n", sum, last);
  return (sum == 10 && last == 0) ? 0 : 1;
}
