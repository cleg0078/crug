/* crug-BORROW-016: a shared borrow while a mutable borrow is live.  */
void f (void)
{
  own int *p = (int *) 1;
  mut int *m = borrow_mut (p);
  ref int *r = borrow (p);
  (void) m;
  (void) r;
}
