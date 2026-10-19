/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* The value is owned but has no destructor, so it will leak.  */

struct S { int x; };
extern struct S *raw_source;

void
f (void)
{
  own struct S *p = raw_source; /* { dg-warning "never adopted or released" } */
  (void) p;
}
