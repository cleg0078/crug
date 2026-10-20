/* crug-FFI-009: a callback argument must be a function or null.  */
extern void run_cb (void (*f) (void));

int main (void)
{
  static int x;
  run_cb ((void (*) (void)) &x);
  return 0;
}
