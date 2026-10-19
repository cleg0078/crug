/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  mut int *m = borrow_mut (p);
  ref int *r = borrow (p); /* { dg-error "crug-BORROW-016" } */
  (void) m;
  (void) r;
}
