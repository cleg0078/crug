/* unsafe and inline asm.
 *
 * An asm statement can change memory that the ownership analysis tracks, so
 * strict mode confines it to an unsafe block (crug-SAFE-004).
 *
 *   gcc/crugc -O2 -fcrug-strict examples/09-unsafe-asm.c -o build/unsafe-asm
 */
#include <stdio.h>

static int add_one_asm (int v)
{
  int out = v;
#if defined(__x86_64__) || defined(__i386__)
  unsafe {
    asm ("leal 1(%1), %0" : "=r" (out) : "r" (v));
  }
#else
  out = v + 1;                        /* keep the example portable */
#endif
  return out;
}

int main (void)
{
  printf ("asm gives %d\n", add_one_asm (41));
  return 0;
}
