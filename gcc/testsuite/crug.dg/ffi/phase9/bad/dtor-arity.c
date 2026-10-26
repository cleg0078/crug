#include "../contracts.h"

/* A destructor takes exactly one pointer argument; this one takes two, so the
   runtime could not call it with the value it has to release (ADR 0007 D5,
   crug-FFI-011).  */
void bad_arity (Node *n, int extra);

int main (void) { return 0; }
