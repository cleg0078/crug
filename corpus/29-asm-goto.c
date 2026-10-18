int compare(int a, int b)
{
  int result = 0;
  __asm__ goto ("cmpl %1, %0; je %l2" : : "r" (a), "r" (b) : "cc" : equal);
  return -1;
equal:
  result = 0;
  return result;
}
