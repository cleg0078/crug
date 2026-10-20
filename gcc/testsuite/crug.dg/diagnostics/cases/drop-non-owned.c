/* crug-DROP-001: drop() needs an owned operand.  */
struct S { int x; };

int f (void)
{
  struct S *raw = (struct S *) 0;
  drop (raw);
  return 0;
}
