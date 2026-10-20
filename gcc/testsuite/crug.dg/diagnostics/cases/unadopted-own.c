/* crug-OWN-007: an owned local is neither released nor dropped.  */
struct Foo { int v; };
extern struct Foo *raw;

void f (void)
{
  own struct Foo *p = raw;
  (void) p;
}
