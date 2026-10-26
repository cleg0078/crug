#include "../contracts.h"

/* An old-style definition has no prototype, so the parameter type of the
   destructor cannot be checked and the contract is rejected (crug-FFI-011).  */
void bad_knr (n)
     Node *n;
{
  (void) n;
}

int main (void) { return 0; }
