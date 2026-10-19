/* { dg-do run } */
#include "contracts.h"
int main (void)
{
  {
    own Node *n = my_alloc (3);
    if (n == 0)
      return 1;
    if (n->v != 3)
      return 2;
  }
  return live_count () == 0 ? 0 : 3;
}
