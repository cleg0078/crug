/* A program the compiler rejects: a mutable borrow while a shared borrow is
 * still live.  crug-BORROW-002 is the code the phase exists for - the shared
 * view would be invalidated by the write, and nothing at run time would say so.
 *
 *   ./gcc/crugc -O2 -fcrug-borrow -fsyntax-only examples/06-borrow-conflict.c
 *
 * expected: error: cannot mutably borrow 'c' while it is shared-borrowed
 *           [crug-BORROW-002]
 *
 * The fix in a real program is either to end the shared borrow first (put it in
 * its own block) or to take the mutable borrow alone.
 */

#include <stdlib.h>

struct counter
{
  int value;
};

int
main (void)
{
  own struct counter *c = adopt (malloc (sizeof *c), free);
  c->value = 0;

  ref struct counter *r = borrow (c);
  mut struct counter *m = borrow_mut (c);
  m->value = r->value + 1;

  return 0;
}
