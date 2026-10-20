/* { dg-do compile } */
/* { dg-options "-fcrug-ownership -Wno-crug-unadopted-own" } */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void double_move (void)
{
  own struct Foo *a = raw_source;
  own struct Foo *b = move (a);
  own struct Foo *c = move (a); /* { dg-error "move of already-moved" } */
  *sink = b;
  *sink = c;
}
