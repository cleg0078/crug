/* Zero-cost case: an owned allocation is destroyed automatically.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The plain twin performs the same free, guarded for a NULL result the way
   safe C code does.  The claim under test is that automatic drop adds no
   instruction and no call relative to that twin.  */

#include <stdlib.h>

void
own_drop (void)
{
  own int *p = malloc (sizeof (int));
  if (p)
    *p = 1;
}
