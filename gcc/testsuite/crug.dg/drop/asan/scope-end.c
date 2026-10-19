#include <stdlib.h>
#include <stdio.h>

struct S { int x; };

int
main (void)
{
  {
    own struct S *p = malloc (sizeof *p);
    if (p == NULL)
      return 2;
    p->x = 1;
  }
  puts ("ok");
  return 0;
}
