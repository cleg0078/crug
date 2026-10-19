/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

struct Foo { int x; };
extern struct Foo *raw_source;
extern struct Foo **sink;

void use_after_move (void)
{
  own struct Foo *p = raw_source;
  own struct Foo *q = move (p);
  *sink = p; /* { dg-error "use of moved value" } */
  *sink = q;
}
