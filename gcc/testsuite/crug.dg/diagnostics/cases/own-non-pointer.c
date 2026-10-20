/* crug-TYPE-001: an owning declarator must have pointer type.  */
void f (void)
{
  own int x;
  (void) x;
}
