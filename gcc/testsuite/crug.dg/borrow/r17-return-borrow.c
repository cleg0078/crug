/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
ref int *f (void) /* { dg-error "crug-BORROW-001" } */
{
  return 0;
}
