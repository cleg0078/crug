/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -fcrug-alias-facts -Wno-crug-unadopted-own" } */

typedef void (*op) (mut int *, ref const int *, int);

void
f (op fn, own int *d)
{
  /* An indirect call has no callee contract to prove disjointness.  */
  fn (borrow_mut (d), d, 1); /* { dg-error "crug-BORROW-024" } */
  /* { dg-error "crug-BORROW-020" "" { target *-*-* } 10 } */
}
