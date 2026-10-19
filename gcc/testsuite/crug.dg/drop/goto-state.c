/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* The label is reached with different ownership states: one path released
   the value, the other still owns it.  */

struct S { int x; };
extern struct S *raw_source;

void
f (int c)
{
  own struct S *p = raw_source;
  if (c)
    goto out; /* { dg-error "destroyed exactly once" } */
  release (p);
out:
  ;
}
