/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

#include <stdlib.h>

/* An owned value initialized by a conditional move owns the value the taken
   arm moved in, so the destructor of the moved-from object carries over to
   the new binding (crug-OWN-007 must not fire).  */

void
f (int cond)
{
  own int *p = malloc (4);
  own int *q = cond ? move (p) : move (p);
  (void) q;
}
