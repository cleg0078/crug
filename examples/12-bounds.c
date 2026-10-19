/* Checked slice indexing: the last access is out of range and traps.
   Build and run:
     ./gcc/crugc -O2 -fcrug-slices examples/12-bounds.c -o build/bounds
     ./build/bounds; echo "exit=$?"     # exit=132 (SIGILL)

   With -fno-crug-bounds-checks the trap is gone: the program reads past the
   slice and prints a value the caller can no longer trust.  */

#include <stdio.h>

static int read_at (slice<int> values, size_t i)
{
  return values[i];
}

int main (void)
{
  int a[2] = { 7, 8 };

  printf ("a[1]=%d\n", read_at (slice (a, 2), 1));
  printf ("a[2]=%d\n", read_at (slice (a, 2), 2));  /* traps here */
  return 0;
}
