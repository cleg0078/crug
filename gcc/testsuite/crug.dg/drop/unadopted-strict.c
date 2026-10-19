/* { dg-do compile } */
/* { dg-options "-fcrug-ownership -fcrug-strict" } */

struct S { int x; };
extern struct S *raw_source;

void
f (void)
{
  own struct S *p = raw_source; /* { dg-error "never adopted or released" } */
  (void) p;
}
