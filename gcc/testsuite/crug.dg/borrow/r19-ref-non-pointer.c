/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  ref int x; /* { dg-error "crug-TYPE-007" } */
  (void) x;
}
