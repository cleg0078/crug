/* ref and mut are borrows: a shared view and an exclusive view of the same
 * owned object.  They are erased at lowering, so the callee reads a plain
 * pointer and nothing is refcounted (RFC-0001 section 18).
 *
 *   ./gcc/crugc -O2 -fcrug-borrow examples/05-borrow.c -o build/borrow
 *
 * A borrow may name a whole owned object.  Reads through a shared borrow may
 * be many at once; a mutable borrow must be alone.  Each borrow here lives to
 * the closing brace of the block that declared it; example 07 shows the phase 7
 * rule, where a borrow ends at its last use instead.
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
read_twice (ref struct counter *c)
{
  return c->value + c->value;
}

static void
set (mut struct counter *c, int v)
{
  c->value = v;
}

int
main (void)
{
  own struct counter *c = adopt (malloc (sizeof *c), destroy_counter);
  c->value = 2;

  {
    ref struct counter *r = borrow (c);
    printf ("read %d\n", read_twice (r));
  }

  {
    mut struct counter *m = borrow_mut (c);
    set (m, 41);
  }

  {
    ref struct counter *r = borrow (c);
    printf ("after the mutable borrow: %d\n", read_twice (r));
  }

  return 0;
}
