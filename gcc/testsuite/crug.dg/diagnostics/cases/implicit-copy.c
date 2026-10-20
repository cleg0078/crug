/* crug-UAM-003: an owned value cannot be copied implicitly.  */
struct Foo { int v; };
extern struct Foo *raw_source;

void f (void)
{
  own struct Foo *a = raw_source;
  own struct Foo *b = a;
  (void) b;
}
