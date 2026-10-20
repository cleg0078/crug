/* crug-UAM-001: use of an owned value after it was moved.  */
struct Foo { int v; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void f (void)
{
  own struct Foo *p = raw_source;
  own struct Foo *q = move (p);
  *sink = p;
  (void) q;
}
