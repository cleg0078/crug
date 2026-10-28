/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: a conditional drop () leaves the object maybe-moved, so a
   second unconditional drop () cannot be proven to destroy it exactly once.  */

#include <stdlib.h>

int cond;

void f (void)
{
  own int *p = malloc (4); /* { dg-error "crug-DROP-006" } */
  int r = cond ? (drop (p), 1) : 0;
  drop (p);
  (void) r;
}
