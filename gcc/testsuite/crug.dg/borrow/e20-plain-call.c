/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct Foo { int v; };
static void use_plain (struct Foo *p)
{
  (void) p;
}

void f (own struct Foo *p)
{
  ref struct Foo *a = borrow (p);
  use_plain (a); /* { dg-error "crug-BORROW-020" } */
}
