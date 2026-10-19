/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct Foo { int v; };

int add_via_ref (own struct Foo *raw)
{
  ref struct Foo *r = borrow (raw);
  return r->v;
}

void set_via_mut (own struct Foo *raw, int v)
{
  mut struct Foo *m = borrow_mut (raw);
  m->v = v;
}
