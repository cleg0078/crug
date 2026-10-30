/* Regression: the sidecar ends right after 'fn', before the mandatory
   function name.  The parser must report crug-FFI-013 instead of failing
   silently (fix-p3, FFI-01).  */

int
main (void)
{
  return 0;
}
