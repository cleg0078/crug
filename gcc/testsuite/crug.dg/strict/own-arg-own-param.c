/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
struct Foo { int v; };
void destroy (struct Foo *p) { (void) p; }
struct Foo *source (void);

void take_own (own struct Foo *p)
{
  own struct Foo *q = adopt (p, destroy);
  (void) q;
}

void call (void)
{
  own struct Foo *x = adopt (source (), destroy);
  take_own (x); /* { dg-error "crug-OWN-011" } */
}
