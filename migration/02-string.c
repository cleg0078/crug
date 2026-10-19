/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample: a string from strdup, released once. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main (void)
{
  char *text = strdup ("c2crug sample");

  printf ("02-string %d %s\n", (int) strlen (text), text);

  free (text);
  return 0;
}
