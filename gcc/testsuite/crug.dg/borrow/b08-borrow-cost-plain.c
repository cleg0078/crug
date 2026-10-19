/* { dg-do compile } */
/* { dg-options "-O2" } */
struct Foo { int v; };

int add_via_ref (struct Foo *raw)
{
  struct Foo *r = raw;
  return r->v;
}

void set_via_mut (struct Foo *raw, int v)
{
  struct Foo *m = raw;
  m->v = v;
}
