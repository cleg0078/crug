/* { dg-do run } */
/* No explicit sidecar is named: contract.crugffi sits next to the quoted
   include and is discovered because of -fcrug-ffi-discover.  */
#include "contract.h"
#include <stdlib.h>

static int live;

Node *adj_alloc (int v)
{
  Node *n = (Node *) malloc (sizeof (Node));
  if (n) { n->v = v; live++; }
  return n;
}

void adj_free (Node *n)
{
  if (n) { live--; free (n); }
}

int main (void)
{
  {
    own Node *n = adj_alloc (2);
    if (n == 0 || n->v != 2 || live != 1)
      return 1;
  }
  return live == 0 ? 0 : 2;
}
