/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
struct Foo { int v; };
void plain (struct Foo *p);
void destroy (struct Foo *p) { (void) p; }
struct Foo *source (void);

void call (void)
{
  own struct Foo *x = adopt (source (), destroy);
  plain (x); /* { dg-error "crug-OWN-012" } */
}
