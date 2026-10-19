/* { dg-do run } */
/* A 'mut_borrows' parameter takes a mutable view.  */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (1);
  if (n == 0)
    return 1;
  {
    mut Node *m = borrow_mut (n);
    set_value (m, 8);
  }
  return n->v == 8 ? 0 : 2;
}
