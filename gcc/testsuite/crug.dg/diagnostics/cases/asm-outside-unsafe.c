/* crug-SAFE-004: inline assembly outside unsafe.  */
void f (void)
{
  asm volatile ("" ::: "memory");
}
