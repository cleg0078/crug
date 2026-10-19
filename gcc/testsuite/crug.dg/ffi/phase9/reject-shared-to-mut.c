/* { dg-do compile } */
/* A shared borrow cannot satisfy 'mut_borrows'.  */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (1);
  if (n == 0)
    return 1;
  ref Node *r = borrow (n);
  set_value (r, 2);
  return 0;
}
