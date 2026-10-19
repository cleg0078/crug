/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
void f (int c, int x)
{
  own int *p = (int *) 1;
  ref int *a = borrow (p);
  if (c)
    x = *a;
  else
    x = *a + 1;
  mut int *m = borrow_mut (p); /* { dg-error "crug-BORROW-002" } */
  *m = x;
  x += *a;
  (void) x;
}
