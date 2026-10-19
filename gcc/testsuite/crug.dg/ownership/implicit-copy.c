/* { dg-do compile } */
/* { dg-options "-fcrug-ownership -Wno-crug-unadopted-own" } */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void implicit_copy (void)
{
  own struct Foo *a = raw_source;
  own struct Foo *b = a; /* { dg-error "implicit copy of owned value" } */
  *sink = b;
}
