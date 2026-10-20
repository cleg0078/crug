/* crug-OWN-001: move() needs an owned operand.  */
struct Foo { int v; };
extern struct Foo *raw;
extern struct Foo *sink;

void f (void)
{
  struct Foo *p = raw;
  sink = move (p);
}
