/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
int main (void)
{
  int ref = 1;
  int mut = 2;
  int borrow = 3;
  int borrow_mut = 4;
  return ref + mut + borrow + borrow_mut == 10 ? 0 : 1;
}
