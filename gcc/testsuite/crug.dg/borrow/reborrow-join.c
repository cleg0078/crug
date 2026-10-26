/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

void
f (int c, own int *p, own int *q)
{
  ref int *r;
  if (c)
    r = borrow (p);
  else
    r = borrow (q);
  /* r borrows different objects on the two paths.  */
  ref int *s = borrow (r); /* { dg-error "crug-BORROW-024" } */
  (void) s;
}
