/* { dg-do compile } */
/* A 'consumes' argument must be moved explicitly.  */
#include "contracts.h"
int main (void)
{
  own Node *n = my_alloc (4);
  my_free (n);
  return 0;
}
