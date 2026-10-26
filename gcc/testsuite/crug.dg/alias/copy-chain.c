/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  int *alias = d;
  /* The plain copy aliases d even though it was made in another variable.  */
  add_into (borrow_mut (d), alias, 4); /* { dg-error "crug-BORROW-024" } */
}
