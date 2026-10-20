/* crug-CONC-011: an owned value passed to a task without transfer.  */
#include <stdlib.h>

static void take (own char *p)
{
  release (p);
}

int main (void)
{
  own char *buf = malloc (4);

  go take (buf);
  release (buf);
  return 0;
}
