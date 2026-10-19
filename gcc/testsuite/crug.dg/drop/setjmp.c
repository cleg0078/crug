/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* longjmp does not run destructors between the jump and the landing pad.  */

struct S { int x; };
extern struct S *raw_source;
extern int setjmp (void *);
static void my_free (void *p) { (void) p; }

void
f (void)
{
  own struct S *p = adopt (raw_source, my_free);
  setjmp (0); /* { dg-warning "may be skipped" } */
}
