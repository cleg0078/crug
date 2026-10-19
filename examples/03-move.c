/* move() transfers ownership; adopt() in the callee attaches the destructor.
 *
 *   ./gcc/crugc -O2 -fcrug-ownership examples/03-move.c -o build/move
 *
 * After the move the caller may not use the source again (crug-UAM-001);
 * the move itself compiles to a register copy, never to a call
 * (RFC-0001 section 16).
 *
 * An own parameter is owned by the callee (ADR 0005 A-1), but a parameter
 * carries no destructor: the destructor travels with the analysis state of a
 * declaration, not with a pointer value.  So the callee adopts the parameter
 * explicitly when it wants automatic destruction, which is the same rule the
 * caller follows for a malloc() result.
 */

#include <stdio.h>
#include <stdlib.h>

struct node
{
  int value;
};

static void
destroy_node (struct node *n)
{
  printf ("destroy %d\n", n->value);
  free (n);
}

static void
consume (struct node *raw)
{
  own struct node *n = adopt (raw, destroy_node);
  printf ("consume %d\n", n->value);
}

int
main (void)
{
  own struct node *a = adopt (malloc (sizeof *a), destroy_node);
  a->value = 42;
  consume (move (a));
  printf ("the caller no longer owns the node\n");
  return 0;
}