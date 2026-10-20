/* crug-DROP-002: a goto that jumps into the scope of an owned object.  */
struct S { int x; };
extern struct S *raw_source;

void f (int c)
{
  if (c)
    goto inside;
  {
    own struct S *p = raw_source;
inside:
    p->x = 1;
  }
}
