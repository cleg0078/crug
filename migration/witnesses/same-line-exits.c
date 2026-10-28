/* Audit witness R4-TOOL: two early exits share one physical line.  The
   lexical guard scan of c2crug 0.2.3 compares only the line number, so the
   null guard of the first exit is credited to the second exit as well: the
   tool rewrites the pointer to own although one return still skips the
   release, and the migrated program no longer compiles (Crug rejects the
   path that leaves the own value without its drop).  */
#include <stdio.h>
#include <stdlib.h>

static int
take (int bail)
{
  int *p = malloc (sizeof (int));

  if (p == 0) return 1; if (bail) return 2;
  p[0] = 7;
  free (p);
  return 0;
}

int
main (void)
{
  int rc = take (0);

  if (rc != 0)
    return rc;
  printf ("same-line-exits %d\n", rc);
  return 0;
}
