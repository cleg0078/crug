/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (int x)
{
  own int *p = (int *) 1;
  mut int *m = borrow_mut (p);
  ref int *a = borrow (p); /* { dg-error "crug-BORROW-016" } */
  x = *a;
  *m = x;
  (void) x;
}
