/* Audit witness R4-TOOL: this translation unit redefines free, so the
   "release" the raw-text scanner sees is not a release at all.  The
   migrated program frees storage that the program still uses.  */
#include <stdio.h>
#include <stdlib.h>

static int *pool[4];
static int used;

static void
keep (int *q)
{
  pool[used++] = q;
}

#define free(q) keep (q)

static int
stash (void)
{
  int *p = malloc (sizeof (int));

  p[0] = 3;
  free (p);
  return 0;
}

int
main (void)
{
  if (stash () != 0)
    return 1;
  printf ("free-redefinition %d\n", pool[0][0]);
  return 0;
}
