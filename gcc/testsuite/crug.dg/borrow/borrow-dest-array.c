/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

struct Foo { int v; };

void
f (own struct Foo *p)
{
  struct Foo *slots[2];
  slots[0] = borrow (p); /* { dg-error "crug-BORROW-018" } */
}
