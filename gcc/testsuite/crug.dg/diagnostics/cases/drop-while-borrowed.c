/* crug-BORROW-005: the owner is dropped while it is borrowed.  */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  drop (p);
  (void) r;
}
