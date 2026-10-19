/* Declarations of the Phase 9 contract fixture library (ADR 0007 D3).
   Every function a sidecar names must be declared in the translation unit
   that loads the sidecar, so tests include this header.  */

#ifndef CRUG_FFI_CONTRACTS_H
#define CRUG_FFI_CONTRACTS_H

typedef struct Node { int v; } Node;

Node *my_alloc (int v);
void my_free (Node *n);
void fill_node (Node **out);
int peek (Node *n);
void set_value (Node *n, int v);
void run_cb (void (*f) (void));
int live_count (void);

#endif
