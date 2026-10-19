/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership" } */

struct S { int x; };

int
f (void)
{
  struct S *raw = (struct S *) 0;
  drop (raw); /* { dg-error "crug-DROP-001" } */
  return 0;
}
