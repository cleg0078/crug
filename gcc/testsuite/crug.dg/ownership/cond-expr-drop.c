/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: a drop () in one arm of a conditional destroys the value on
   that path only.  The guarded destruction at the end of the scope destroys
   it on the other path, so the value is destroyed exactly once either way and
   no diagnostic is due.  Before the fix the arm walked over the state of the
   path that skips it, and the leaking path was accepted silently.  */

#include <stdlib.h>

int cond;

void f (void)
{
  own int *p = malloc (4);
  int r = cond ? (drop (p), 1) : 0;
  (void) r;
}
