#include "../contracts.h"

/* A variadic function cannot be a destructor: the type of its one argument is
   not fixed (crug-FFI-011).  */
void bad_variadic (Node *n, ...);

int main (void) { return 0; }
