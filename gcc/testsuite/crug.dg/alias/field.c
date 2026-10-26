/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

void add_into (mut int *d, ref const int *a, int n);

struct S { int *p; };

void
f (own int *d)
{
  struct S s;
  s.p = d;
  /* A pointer loaded out of a structure has no proven root.  */
  add_into (borrow_mut (d), s.p, 1); /* { dg-error "crug-BORROW-024" } */
}
