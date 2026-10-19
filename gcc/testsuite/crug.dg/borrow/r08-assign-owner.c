/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  p = (int *) 2; /* { dg-error "crug-BORROW-014" } */
  (void) r;
}
