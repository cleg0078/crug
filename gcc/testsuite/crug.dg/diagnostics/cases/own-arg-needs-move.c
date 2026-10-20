/* crug-OWN-011: an owned argument must be moved explicitly.  */
struct Foo { int v; };
static void destroy (struct Foo *p) { (void) p; }
extern struct Foo *source (void);
extern void take_own (own struct Foo *p);

void f (void)
{
  own struct Foo *x = adopt (source (), destroy);
  take_own (x);
}
