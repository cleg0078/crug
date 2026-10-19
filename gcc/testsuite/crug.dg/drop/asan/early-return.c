#include <stdlib.h>
#include <stdio.h>

struct S { int x; };

static int
f (int c)
{
  own struct S *p = malloc (sizeof *p);
  if (p == NULL)
    return 2;
  p->x = c;
  if (c)
    return p->x;
  return 0;
}

int
main (void)
{
  if (f (1) != 1)
    return 1;
  puts ("ok");
  return 0;
}
