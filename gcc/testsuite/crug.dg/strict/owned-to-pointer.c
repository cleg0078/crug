/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
struct Foo { int v; };
void destroy (struct Foo *p) { (void) p; }
struct Foo *source (void);

void convert (void)
{
  own struct Foo *a = adopt (source (), destroy);
  void *q = (void *) a; /* { dg-error "crug-SAFE-003" } */
  (void) q;
}
