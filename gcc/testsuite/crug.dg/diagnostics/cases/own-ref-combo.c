/* crug-TYPE-002: more than one Crug qualifier on one declarator.  */
void f (void)
{
  own ref int *p;
  (void) p;
}
