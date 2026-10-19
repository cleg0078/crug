#ifndef ADJACENT_CONTRACT_H
#define ADJACENT_CONTRACT_H
typedef struct Node { int v; } Node;
Node *adj_alloc (int v);
void adj_free (Node *n);
#endif
