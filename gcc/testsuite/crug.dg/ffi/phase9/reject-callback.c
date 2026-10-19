/* { dg-do compile } */
/* A callback argument must be a function or a null pointer.  */
#include "contracts.h"
int main (void)
{
  static int x;
  run_cb ((void (*) (void)) &x);
  return 0;
}
