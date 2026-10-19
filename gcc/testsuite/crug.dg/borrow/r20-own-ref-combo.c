/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own ref int *p; /* { dg-error "crug-TYPE-002" } */
  (void) p;
}
