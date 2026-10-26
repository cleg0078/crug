/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

void
f (void)
{
  own int *p;
  /* Dropping an empty owned value is a no-op, not a read.  */
  drop (p);
}
