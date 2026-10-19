/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  mut int *m = borrow_mut (p);
  int x = *p; /* { dg-error "crug-BORROW-015" } */
  (void) m;
  (void) x;
}
