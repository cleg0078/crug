/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership" } */
/* Hybrid mode accepts what strict mode rejects.  */
struct Foo { int v; };

int read_it (raw struct Foo *p)
{
  return p->v;
}
