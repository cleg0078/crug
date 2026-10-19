/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* ADR 0004: a flexible array member cannot be an owning array; the
   qualifier must go on a complete array or on a pointer.  */

struct Foo { int x; };

struct Table
{
  int n;
  own struct Foo *items[]; /* { dg-error "qualifier is not allowed" } */
};
