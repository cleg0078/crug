/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
/* The gate fixture: this file compiles without -fcrug-strict and is
   rejected with it at the dereference.  */
struct Foo { int v; };

int read_it (raw struct Foo *p)
{
  return p->v; /* { dg-error "crug-SAFE-001" } */
}
