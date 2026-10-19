/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  own int *q = move (p); /* { dg-error "crug-BORROW-004" } */
  (void) r;
  (void) q;
}
