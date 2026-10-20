/* Plain-C twin of own-drop/crug.c.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdlib.h>

void
own_drop (void)
{
  int *p = malloc (sizeof (int));
  if (p)
    *p = 1;
  if (p)
    free (p);
}
