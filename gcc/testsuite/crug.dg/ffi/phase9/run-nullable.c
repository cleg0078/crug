/* { dg-do run } */
/* A nullable owned return accepts null, and null is never destroyed.
   The fixture library returns null for a negative request.  */
#include "contracts.h"
int main (void)
{
  {
    own Node *n = my_alloc (-1);
    if (n != 0)
      return 1;
    my_free (0);
  }
  return live_count () == 0 ? 0 : 2;
}
