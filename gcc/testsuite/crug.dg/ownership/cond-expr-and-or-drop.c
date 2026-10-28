/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: the right operand of && / || may not run at all, so a drop ()
   inside it is an alternative of its own: conditional on that path, and
   repaired by the guarded destruction of the scope on the other path.  */

#include <stdlib.h>

void f (int a, int b)
{
  own int *p = malloc (4);
  int r = a && (drop (p), 1);

  own int *q = malloc (4);
  int s = b || (drop (q), 1);

  (void) r;
  (void) s;
}
