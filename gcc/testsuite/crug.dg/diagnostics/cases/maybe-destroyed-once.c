/* crug-DROP-006: destruction is not provably exactly once on this path.  */
struct Foo { int x; };
extern struct Foo *raw_source;
extern int cond;
static void my_free (void *p) { (void) p; }

void f (void)
{
  own struct Foo *p = adopt (raw_source, my_free);
  if (cond)
    move (p);
}
