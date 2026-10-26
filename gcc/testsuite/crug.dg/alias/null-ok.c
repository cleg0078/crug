/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  /* A null pointer aliases nothing.  */
  add_into (borrow_mut (d), 0, 1);
}
