/* A rejected program: the compiler proves the use after the move.
 *
 *   ./gcc/crugc -O2 -fcrug-ownership -fsyntax-only examples/04-use-after-move.c
 *
 * Expected: error: use of moved value 'a' [crug-UAM-001]
 * Exit status 1.  The code is kept here because the diagnostic is the
 * feature: ownership is checked statically, with no runtime cost.
 */

#include <stdlib.h>

struct node
{
  int value;
};

int
main (void)
{
  own struct node *a = malloc (sizeof *a);
  own struct node *b = move (a);
  a->value = 1; /* use after move */
  return b->value;
}