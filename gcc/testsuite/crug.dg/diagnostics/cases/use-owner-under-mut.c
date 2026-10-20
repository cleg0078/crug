/* crug-BORROW-015: the owner is read while mutably borrowed.  */
void f (void)
{
  own int *p = (int *) 1;
  mut int *m = borrow_mut (p);
  int x = *p;
  (void) m;
  (void) x;
}
