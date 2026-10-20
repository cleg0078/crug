/* crug-OWN-002: the destructor has the wrong signature for the operand.  */
struct Foo { int v; };
extern struct Foo *my_alloc (void);
static int not_a_dtor (struct Foo *p) { (void) p; return 0; }

void f (void)
{
  own struct Foo *p = adopt (my_alloc (), not_a_dtor);
  release (p);
}
