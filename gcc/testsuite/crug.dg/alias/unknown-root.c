/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d, int *unknown)
{
  /* The root of a plain parameter is not known, so no disjointness fact.  */
  add_into (borrow_mut (d), unknown, 1); /* { dg-error "crug-BORROW-024" } */
}
