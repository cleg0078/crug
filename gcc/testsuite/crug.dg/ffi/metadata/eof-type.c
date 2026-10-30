/* Regression: the sidecar ends right after the tag keyword, so the parser
   needs a name that does not exist.  That used to fail without a diagnostic;
   it is now crug-FFI-013 (fix-p3, FFI-01).  */

int
main (void)
{
  return 0;
}
