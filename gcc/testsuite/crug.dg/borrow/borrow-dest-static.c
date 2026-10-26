/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

struct Foo { int v; };

void
f (own struct Foo *p)
{
  static ref struct Foo *saved; /* { dg-error "crug-BORROW-018" } */
  saved = borrow (p); /* { dg-error "crug-BORROW-018" } */
}
