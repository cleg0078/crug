/* crug-DROP-008: assignment to an owned value would leak it.  */
struct S { int x; };
extern struct S *raw_source;

void f (void)
{
  own struct S *p = raw_source;
  p = raw_source;
}
