/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  int *alias = d;
  int **pp = &alias;
  /* Loading through a pointer to pointer hides the root.  */
  add_into (borrow_mut (d), *pp, 1); /* { dg-error "crug-BORROW-024" } */
}
