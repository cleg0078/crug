/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

void
f (void)
{
  own int *p;
  int **pp = &p; /* { dg-error "crug-UAM-008" } */
  (void) pp;
}
