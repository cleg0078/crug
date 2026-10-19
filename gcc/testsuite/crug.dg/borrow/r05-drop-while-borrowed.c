/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  drop (p); /* { dg-error "crug-BORROW-005" } */
  (void) r;
}
