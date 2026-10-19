/* Strict mode: the same source, two answers.
 *
 * Hybrid mode (-fcrug-ownership) accepts the raw dereference.
 * Strict mode (-fcrug-strict) rejects it with crug-SAFE-001, and an unsafe
 * block is the way to keep it.
 *
 *   gcc/crugc -O2 -fcrug-ownership examples/08-strict-mode.c -o build/strict-hybrid
 *   gcc/crugc -O2 -fcrug-strict      examples/08-strict-mode.c -o build/strict-strict
 */
#include <stdio.h>

struct Point { int x; int y; };

/* A raw pointer: a plain view with no ownership. */
static int sum_raw (raw struct Point *p)
{
  return p->x + p->y;                 /* strict mode rejects this line */
}

static int sum_unsafe (raw struct Point *p)
{
  int s;
  unsafe { s = p->x + p->y; }         /* strict mode accepts this block */
  return s;
}

int main (void)
{
  struct Point pt = { 3, 4 };
  printf ("raw     %d\n", sum_raw (&pt));
  printf ("unsafe  %d\n", sum_unsafe (&pt));
  return 0;
}
