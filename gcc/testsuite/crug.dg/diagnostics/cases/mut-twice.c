/* crug-BORROW-003: a second mutable borrow of the same owner.  */
void f (void)
{
  own int *p = (int *) 1;
  mut int *a = borrow_mut (p);
  mut int *b = borrow_mut (p);
  (void) a;
  (void) b;
}
