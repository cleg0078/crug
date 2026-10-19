/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
void *make (unsigned long i)
{
  return (void *) i; /* { dg-error "crug-SAFE-002" } */
}

unsigned long back (void *p)
{
  return (unsigned long) p; /* { dg-error "crug-SAFE-002" } */
}
