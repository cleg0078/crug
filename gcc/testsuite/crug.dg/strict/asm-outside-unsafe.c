/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
void poke (void)
{
  asm volatile ("" ::: "memory"); /* { dg-error "crug-SAFE-004" } */
}
