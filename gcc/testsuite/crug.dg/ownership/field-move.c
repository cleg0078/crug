/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* ADR 0004 D4: partial moves are post-MVP.  */

struct Foo { int x; };
struct Holder { struct Foo *f; };

extern struct Foo **sink;

void field_move (struct Holder *h)
{
  move (h->f); /* { dg-error "move of aggregate field" } */
  *sink = h->f;
}
