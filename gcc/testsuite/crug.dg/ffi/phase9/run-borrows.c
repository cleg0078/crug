/* { dg-do run } */
/* A 'borrows' parameter takes a shared view.  */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (9);
  if (n == 0)
    return 1;
  {
    ref Node *r = borrow (n);
    if (peek (r) != 9)
      return 2;
  }
  return 0;
}
