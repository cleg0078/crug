/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

struct S { int x; };
extern struct S *raw_source;
static void my_free (void *p) { (void) p; }

void
f (void)
{
  own struct S *p = adopt (raw_source, my_free);
  drop (p);
  drop (p); /* { dg-error "use of moved value" } */
}
