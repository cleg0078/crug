/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  mut int *m = borrow (p); /* { dg-error "crug-BORROW-012" } */
  (void) m;
}
