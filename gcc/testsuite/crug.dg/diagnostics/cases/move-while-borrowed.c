/* crug-BORROW-004: the owner is moved while it is borrowed.  */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  own int *q = move (p);
  (void) r;
  (void) q;
}
