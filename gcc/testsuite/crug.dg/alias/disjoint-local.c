/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

void
f (own int *d)
{
  int other = 0;
  /* A known local object is disjoint from the owned heap object.  */
  add_into (borrow_mut (d), &other, 1);
}
