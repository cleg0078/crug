/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

#include <stdlib.h>

/* A move () and a fresh allocation both have a known root, so passing them
   to own parameters of the same call cannot alias each other.  Treating the
   root as unknown rejected these valid calls with crug-BORROW-024.  */

void take2 (own int *a, own int *b);

void
f (own int *p, own int *q)
{
  take2 (move (p), move (q));
}

void
g (own int *q)
{
  take2 ((int *) malloc (4), move (q));
}
