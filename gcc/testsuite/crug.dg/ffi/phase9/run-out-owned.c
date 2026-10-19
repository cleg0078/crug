/* { dg-do run } */
#include "contracts.h"
int main (void)
{
  {
    own Node *n;
    fill_node (&n);
    if (n == 0 || n->v != 5)
      return 1;
  }
  return live_count () == 0 ? 0 : 2;
}
