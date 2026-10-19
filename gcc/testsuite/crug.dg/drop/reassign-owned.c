/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* Overwriting an owned value would leak it, so it is diagnosed.  */

struct S { int x; };
extern struct S *raw_source;

void
f (void)
{
  own struct S *p = raw_source;
  p = raw_source; /* { dg-error "assignment to an owned value" } */
}
