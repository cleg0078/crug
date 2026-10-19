/* An owned pointer is destroyed when its scope ends: no free() call.
 *
 *   ./gcc/crugc -O2 -fcrug-ownership examples/02-auto-drop.c -o build/auto-drop
 *
 * adopt (pointer, destructor) attaches the destructor; the compiler inserts a
 * call to it on every path that leaves the scope, in reverse declaration
 * order, as a null-guarded direct call.  No drop flag, no helper function,
 * no reference count (ADR 0003, ADR 0005).
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

int
main (void)
{
  {
    own struct node *n = adopt (malloc (sizeof *n), destroy_node);
    n->value = 7;
    printf ("use %d\n", n->value);
  }
  printf ("after the scope\n");
  return 0;
}