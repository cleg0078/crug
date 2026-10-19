struct Foo { int v; };

int read_it (raw struct Foo *p)
{
  int r;
  unsafe { r = p->v; }
  return r;
}
