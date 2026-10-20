/* crug-FFI-008: an out_owned argument must not overwrite an owned value.  */
typedef struct Node { int v; } Node;
extern Node *my_alloc (int v);
extern void my_free (Node *n);
extern void fill_node (Node **out);

int main (void)
{
  own Node *n = my_alloc (1);
  fill_node (&n);
  return 0;
}
