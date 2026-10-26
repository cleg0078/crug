/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (int c, own int *d)
{
  int other = 0;
  int *alias = c ? d : &other;
  /* One arm aliases d, so no disjointness fact can be emitted.  */
  add_into (borrow_mut (d), alias, 1); /* { dg-error "crug-BORROW-024" } */
}
