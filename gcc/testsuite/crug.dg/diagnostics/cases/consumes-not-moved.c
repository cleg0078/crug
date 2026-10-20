/* crug-FFI-007: a consumes argument must be moved explicitly.  */
typedef struct Node { int v; } Node;
extern Node *my_alloc (int v);
extern void my_free (Node *n);

int main (void)
{
  own Node *n = my_alloc (4);
  my_free (n);
  return 0;
}
