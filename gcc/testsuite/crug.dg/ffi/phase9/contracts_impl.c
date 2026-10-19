/* The plain C implementation of contracts.h.  Compiled without any Crug
   flag: the point of the fixture is that the callee is ordinary C.  */

#include "contracts.h"
#include <stdlib.h>

static int live;

Node *my_alloc (int v)
{
  Node *n;

  if (v < 0)
    return 0;
  n = (Node *) malloc (sizeof (Node));
  if (n == 0)
    return 0;
  n->v = v;
  live++;
  return n;
}

void my_free (Node *n)
{
  if (n == 0)
    return;
  live--;
  free (n);
}

void fill_node (Node **out)
{
  *out = my_alloc (5);
}

int peek (Node *n)
{
  return n->v;
}

void set_value (Node *n, int v)
{
  n->v = v;
}

void run_cb (void (*f) (void))
{
  if (f != 0)
    f ();
}

int live_count (void)
{
  return live;
}
