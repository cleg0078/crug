/* Regression: the sidecar ends right after 'returns', so the mandatory C
   type is missing.  The parser must report crug-FFI-003, never fail
   silently (a truncated sidecar is a diagnostic, not a no-op).  */

int f (void);

int
main (void)
{
  return 0;
}
