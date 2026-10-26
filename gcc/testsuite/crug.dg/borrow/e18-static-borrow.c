/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct Foo { int v; };
static ref struct Foo *global; /* { dg-error "crug-BORROW-018" } */

void f (own struct Foo *p)
{
  global = borrow (p); /* { dg-error "crug-BORROW-018" } */
}
