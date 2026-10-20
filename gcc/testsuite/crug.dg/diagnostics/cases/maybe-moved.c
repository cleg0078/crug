/* crug-UAM-004: the value may or may not have been moved at a join.  */
struct Foo { int v; };
extern struct Foo *raw_source;
extern struct Foo **sink;
extern int cond;

void f (void)
{
  own struct Foo *p = raw_source;
  if (cond)
    *sink = move (p);
  *sink = p;
}
