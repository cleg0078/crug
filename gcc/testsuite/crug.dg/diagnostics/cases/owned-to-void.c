/* crug-SAFE-003: an owned value converted to an unmanaged pointer.  */
struct Foo { int v; };
extern struct Foo *raw_source;

void f (void)
{
  own struct Foo *a = raw_source;
  void *q = (void *) a;
  (void) q;
}
