/* { dg-do compile } */
/* An out_owned argument must not overwrite an owned value.  */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (1);
  fill_node (&n);
  return 0;
}
