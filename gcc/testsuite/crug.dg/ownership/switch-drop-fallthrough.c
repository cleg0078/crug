/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D5: a fallthrough is a real sequence on that path, not an
   alternative: the second drop () uses a value the first arm already moved.  */

#include <stdlib.h>

int cond;

void f (void)
{
  own int *p = malloc (4); /* { dg-error "crug-DROP-006" } */

  switch (cond)
    {
    case 0:
      drop (p);
    case 1:
      drop (p); /* { dg-error "use of moved value" } */
      break;
    default:
      break;
    }
}
