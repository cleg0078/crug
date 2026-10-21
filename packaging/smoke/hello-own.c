/* hello-own.c - the ownership demo: malloc, adopt, move, drop.
 *
 *   crugc -O2 -fcrug-ownership hello-own.c -o hello-own
 *
 * The buffer is adopted in main, moved into take(), which adopts it again and
 * lets the compiler call free() on the way out of its scope.  No free() call
 * in this file: the destructor is the drop the compiler inserts.
 */

#include <stdio.h>
#include <stdlib.h>

struct payload
{
  int value;
};

static void
take (struct payload *raw)
{
  own struct payload *p = adopt (raw, free);
  printf ("own-ok value=%d\n", p->value);
}

int
main (void)
{
  own struct payload *p = adopt (malloc (sizeof *p), free);
  if (p == NULL)
    return 2;

  p->value = 42;
  take (move (p));

  return 0;
}
