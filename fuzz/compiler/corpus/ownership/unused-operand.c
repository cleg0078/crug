/* The intrinsics read their operands, so -Wall -Wextra must not report a
   variable or parameter whose only use is inside move(), adopt() or borrow().
   A regression shows up as an excess-error failure of this test.  */

/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wall -Wextra" } */

struct S
{
  int v;
};

void use (struct S *s);

static void
destroy (struct S *s)
{
  (void) s;
}

void
chain (struct S *raw)
{
  own struct S *a = adopt (raw, destroy);
  own struct S *b = move (a);
  own struct S *c = move (b);
  use (c);
}

void
single (struct S *raw)
{
  own struct S *a = adopt (raw, destroy);
  use (a);
}
