/* crug-SAFE-002: an integer/pointer cast outside unsafe.  */
void *to_pointer (long i)
{
  return (void *) i;
}
