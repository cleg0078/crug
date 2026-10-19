/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  int *q = (int *) 1;
  mut int *m = borrow_mut (q); /* { dg-error "crug-BORROW-010" } */
  (void) m;
}
