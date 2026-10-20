/* crug-BORROW-002: a mutable borrow while a shared borrow is live.  */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  mut int *m = borrow_mut (p);
  (void) r;
  (void) m;
}
