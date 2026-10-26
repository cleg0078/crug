/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  int *slot[1];
  slot[0] = d;
  /* A pointer loaded out of an array has no proven root.  */
  add_into (borrow_mut (d), slot[0], 1); /* { dg-error "crug-BORROW-024" } */
}
