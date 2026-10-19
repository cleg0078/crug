/* A borrow ends at its last use, not at the closing brace (phase 7, ADR 0020).
 * The shared borrow below is dead after its printf, so a mutable borrow of the
 * same object is legal in the same block; when that one is dead too, the owner
 * can be moved while still in the block that created it (phase 6 would have
 * rejected all three).
 *
 *   ./gcc/crugc -O2 -fcrug-borrow examples/07-nll.c -o build/nll
 *
 * The dump says why each borrow ended: add -fcrug-dump=borrow and read the
 * .crug-borrow file GCC writes next to the object (here it is
 * build/nll-07-nll.crug-borrow), whose two end lines are marked (last-use).
 */

#include <stdio.h>
#include <stdlib.h>

struct counter
{
  int value;
};

static void
destroy_counter (struct counter *c)
{
  printf ("destroy %d\n", c->value);
  free (c);
}

static int
read (ref struct counter *c)
{
  return c->value;
}

int
main (void)
{
  own struct counter *c = adopt (malloc (sizeof *c), destroy_counter);
  c->value = 10;

  ref struct counter *r = borrow (c);
  printf ("read %d\n", read (r));

  /* last use of r was the line above: the shared borrow is over.  */
  mut struct counter *m = borrow_mut (c);
  m->value = 11;
  printf ("now %d\n", m->value);

  /* last use of m was the line above, so the owner may move.  */
  own struct counter *moved = move (c);
  printf ("moved %d\n", moved->value);

  return 0;
}
