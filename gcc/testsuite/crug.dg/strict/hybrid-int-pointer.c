/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership" } */
void *make (unsigned long i)
{
  return (void *) i;
}
