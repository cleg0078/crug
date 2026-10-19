#include <stdlib.h>
#include <stdio.h>

struct S { int x; };

int
main (void)
{
  int c = 1;
  own struct S *p = malloc (sizeof *p);
  if (p == NULL)
    return 2;
  if (c)
    {
      p->x = 1;
      putchar ('a');
    }
  else
    {
      p->x = 2;
      putchar ('b');
    }
  puts ("ok");
  return 0;
}
