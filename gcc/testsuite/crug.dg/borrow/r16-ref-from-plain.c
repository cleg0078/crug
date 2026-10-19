/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  int *q = (int *) 1;
  ref int *r = q; /* { dg-error "crug-BORROW-017" } */
  (void) r;
}
