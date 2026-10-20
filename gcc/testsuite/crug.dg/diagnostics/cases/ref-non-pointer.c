/* crug-TYPE-007: a borrow qualifier must apply to a pointer.  */
void f (void)
{
  ref int x;
  (void) x;
}
