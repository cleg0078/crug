struct Foo { int v; };
void destroy (struct Foo *p) { (void) p; }
struct Foo *source (void);

int use (void)
{
  own struct Foo *a = adopt (source (), destroy);
  int v;
  unsafe { v = a->v; }
  return v;
}
