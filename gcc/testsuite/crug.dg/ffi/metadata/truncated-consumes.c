/* Regression: 'fn fuzz_free consumes' ends the sidecar before the argument
   reference.  The parser must report crug-FFI-003, never read past its token
   vector (fuzz finding cd32fa703be053e5).  */

void fuzz_free (void *);

int
main (void)
{
  return 0;
}
