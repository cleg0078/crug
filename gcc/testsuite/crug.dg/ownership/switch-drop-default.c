/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: the arms of a switch are alternatives.  A default: that
   covers every path and every arm that ends in break means the switch has no
   fall-out path, so the pre-switch state is not an extra exit and each arm
   may destroy the value exactly once.  */

#include <stdlib.h>

int cond;

void f (void)
{
  own int *p = malloc (4);

  switch (cond)
    {
    case 0:
      drop (p);
      break;
    default:
      drop (p);
      break;
    }
}
