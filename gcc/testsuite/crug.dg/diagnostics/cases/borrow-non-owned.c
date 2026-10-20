/* crug-BORROW-009: borrow() needs an owned object.  */
void f (void)
{
  int *q = (int *) 1;
  ref int *r = borrow (q);
  (void) r;
}
