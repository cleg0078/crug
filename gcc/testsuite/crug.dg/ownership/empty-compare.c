/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

int
f (void)
{
  own int *p;
  if (p == 0) /* { dg-error "crug-UAM-008" } */
    return 1;
  return 0;
}
