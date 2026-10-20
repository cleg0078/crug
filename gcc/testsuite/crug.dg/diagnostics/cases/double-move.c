/* crug-UAM-002: the same owned value is moved twice.  */
struct Foo { int v; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void f (void)
{
  own struct Foo *p = raw_source;
  *sink = move (p);
  *sink = move (p);
}
