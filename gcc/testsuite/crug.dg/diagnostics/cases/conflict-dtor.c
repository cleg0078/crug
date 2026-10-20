/* crug-OWN-010: the two paths adopt with different destructors.  */
struct Foo { int v; };
static char pool[8];
static void *my_alloc (void) { return pool; }
static void d1 (void *p) { (void) p; }
static void d2 (void *p) { (void) p; }

void f (int c)
{
  own struct Foo *p;
  if (c)
    p = adopt (my_alloc (), d1);
  else
    p = adopt (my_alloc (), d2);
}
