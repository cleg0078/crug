/* Audit witness R4-TOOL: a macro hides a pointer alias from the raw-text
   scan.  c2crug 0.2.3 sees a straight-line malloc/free pair, rewrites the
   pointer to own with confidence 1.00, and the migrated program reads
   storage that the own destructor already freed.  */
#include <stdio.h>
#include <stdlib.h>

static int *saved;

#define KEEP() (saved = p, 0)
#define BAIL() return 0

static int
take (void)
{
  int *p = malloc (sizeof (int));

  p[0] = 42;
  KEEP ();
  BAIL ();
  free (p);
  return 0;
}

int
main (void)
{
  int *q;

  if (take () != 0)
    return 1;
  q = saved;
  if (q == 0)
    return 2;
  printf ("hidden-alias %d\n", q[0]);
  return 0;
}
