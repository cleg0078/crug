/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  mut int *a = borrow_mut (p);
  mut int *b = borrow_mut (p); /* { dg-error "crug-BORROW-003" } */
  (void) a;
  (void) b;
}
