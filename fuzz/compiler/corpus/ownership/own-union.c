/* { dg-do compile } */
/* { dg-options "-fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0004 D6: an owning member in a union needs an active-variant
   contract, which the MVP does not have.  */

struct Foo { int x; };

union Choice
{
  own struct Foo *p; /* { dg-error "own member in union" } */
  int i;
};
