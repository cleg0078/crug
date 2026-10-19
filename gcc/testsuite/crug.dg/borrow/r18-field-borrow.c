/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
struct S
{
  ref int *r; /* { dg-error "crug-BORROW-007" } */
};
