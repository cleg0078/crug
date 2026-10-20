/* crug-BORROW-011: an owned value that is null cannot be borrowed.  */
void f (void)
{
  own int *p = 0;
  ref int *r = borrow (p);
  (void) r;
}
