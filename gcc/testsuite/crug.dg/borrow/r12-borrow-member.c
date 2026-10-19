/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct S { int *v; };
void f (void)
{
  struct S s;
  own struct S *p = &s;
  ref int *r = borrow (p->v); /* { dg-error "crug-BORROW-009" } */
  (void) r;
}
