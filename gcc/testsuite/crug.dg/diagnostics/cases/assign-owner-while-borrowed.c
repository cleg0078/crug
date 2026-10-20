/* crug-BORROW-014: the owner is overwritten while a borrow is live.  */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  p = (int *) 2;
  (void) r;
}
