/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct Foo { int v; };
struct Foo *leak (own struct Foo *p)
{
  ref struct Foo *a = borrow (p);
  return a; /* { dg-error "crug-BORROW-019" } */
}
