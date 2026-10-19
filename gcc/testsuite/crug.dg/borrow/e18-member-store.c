/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct Foo { int v; };
struct Holder { struct Foo *p; };

void f (own struct Foo *p, struct Holder *h)
{
  ref struct Foo *r = borrow (p);
  h->p = r; /* { dg-error "crug-BORROW-018" } */
}
