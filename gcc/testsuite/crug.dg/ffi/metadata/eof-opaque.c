/* Regression: the sidecar ends right after 'opaque', so the type name is
   missing.  The parser must report crug-FFI-013 instead of failing silently
   (fix-p3, FFI-01).  */

int
main (void)
{
  return 0;
}
