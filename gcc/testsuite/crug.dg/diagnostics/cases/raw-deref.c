/* crug-SAFE-001: dereferencing a raw pointer outside unsafe.  */
struct Foo { int v; };

int read_it (raw struct Foo *p)
{
  return p->v;
}
