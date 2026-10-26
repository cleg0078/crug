/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

void sink (own int *p);

void
f (void)
{
  own int *p;
  sink (p); /* { dg-error "crug-UAM-008" } */
}
