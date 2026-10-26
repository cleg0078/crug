/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

struct S { int x; };

int
f (void)
{
  own struct S *p;
  return p->x; /* { dg-error "crug-UAM-008" } */
}
