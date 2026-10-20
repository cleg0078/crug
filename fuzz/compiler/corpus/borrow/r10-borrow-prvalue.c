/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  ref int *r = borrow ((int *) 1); /* { dg-error "crug-BORROW-006" } */
  (void) r;
}
