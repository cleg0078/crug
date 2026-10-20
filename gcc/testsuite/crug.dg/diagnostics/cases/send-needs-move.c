/* crug-CONC-003: sending an owning element needs an explicit move.  */
#include <stdlib.h>

static void give (chan own char *out, own char *p)
{
  send (out, p);
}

int main (void)
{
  chan own char *a = channel (1);
  own char *p = malloc (4);
  give (a, move (p));
  close (a);
  return 0;
}
