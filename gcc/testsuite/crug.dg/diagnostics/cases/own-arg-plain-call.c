/* crug-OWN-012: an owned value cannot be passed to an unannotated parameter.  */
struct Foo { int v; };
static void destroy (struct Foo *p) { (void) p; }
extern struct Foo *source (void);
extern void plain (struct Foo *p);

void f (void)
{
  own struct Foo *x = adopt (source (), destroy);
  plain (x);
}
