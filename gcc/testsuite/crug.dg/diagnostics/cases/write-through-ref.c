/* crug-BORROW-008: assignment through a shared borrow.  */
void f (void)
{
  own int *p = (int *) 1;
  ref int *r = borrow (p);
  *r = 5;
}
