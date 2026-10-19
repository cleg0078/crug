/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = 0;
  ref int *r = borrow (p); /* { dg-error "crug-BORROW-011" } */
  (void) r;
}
