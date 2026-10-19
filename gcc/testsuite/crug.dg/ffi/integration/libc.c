/* libc: the repository's libc.crugffi annotates fopen/fclose, so an owned
   FILE * is closed at the end of its scope with no explicit call.  */

#include <stdio.h>
#include <stdlib.h>

int main (void)
{
  {
    own FILE *a = fopen ("/dev/null", "r");
    own FILE *b = fopen ("/dev/null", "r");
    if (a == 0 || b == 0)
      return 1;
  }
  fputs ("ok libc\n", stdout);
  return 0;
}
