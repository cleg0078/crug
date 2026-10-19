/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  ref int *r;
  {
    own int *p = (int *) 1;
    r = borrow (p); /* { dg-error "crug-BORROW-007" } */
  }
}
