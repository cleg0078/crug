/* { dg-do run } */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (4);
  if (n == 0)
    return 1;
  my_free (move (n));
  return live_count () == 0 ? 0 : 2;
}
