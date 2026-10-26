/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */
#include <stdlib.h>

void
f (void)
{
  own int *p;
  /* Initializing an empty owned value is not a read of it.  */
  p = adopt ((int *) malloc (sizeof *p), free);
  drop (p);
}
