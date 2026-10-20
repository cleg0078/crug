/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  mut int *m = borrow_mut (p); /* { dg-error "crug-BORROW-002" } */
  (void) r;
  (void) m;
}
