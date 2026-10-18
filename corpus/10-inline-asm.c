int add_one(int x)
{
  int r;
  __asm__ volatile ("leal 1(%1), %0" : "=r" (r) : "r" (x));
  return r;
}

void barrier(void) { __asm__ volatile ("" ::: "memory"); }
