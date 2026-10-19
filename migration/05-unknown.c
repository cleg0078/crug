/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample with an unknown ownership origin: the pointer comes from
   a function that c2crug cannot inspect, so it is reported as raw. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
make_message (void)
{
  char *p = malloc (16);

  strcpy (p, "unknown");
  return p;
}

int
main (void)
{
  char *message = make_message ();

  printf ("05-unknown %s\n", message);

  free (message);
  return 0;
}
