/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */

void
f (int n, own int *p)
{
  ref int *r;
  for (int i = 0; i < n; i++)
    r = borrow (p);
  /* The loop body may run zero times, so r may name no lender after it.  */
  ref int *s = borrow (r); /* { dg-error "crug-BORROW-024" } */
  (void) s;
}
