/* Audit witness R4-TOOL: a macro hides a release and a nested return from
   the raw-text scan.  c2crug 0.2.3 believes the release is unique and
   straight-line, rewrites it to drop, and the migrated program frees the
   same object twice when the hidden branch runs.  */
#include <stdio.h>
#include <stdlib.h>

#define BAIL_IF(c) do { if (c) { free (p); return 2; } } while (0)

static int
take (int mode)
{
  int *p = malloc (sizeof (int));

  p[0] = 5;
  BAIL_IF (mode);
  free (p);
  return 0;
}

int
main (void)
{
  int rc = take (1);

  printf ("nested-return %d\n", rc);
  return 0;
}
