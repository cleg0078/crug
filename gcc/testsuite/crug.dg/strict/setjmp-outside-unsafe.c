/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-strict" } */
typedef struct { int v[8]; } jmp_buf;
extern int setjmp (jmp_buf *);
extern void longjmp (jmp_buf *, int);

jmp_buf env;

void jump (void)
{
  longjmp (&env, 1); /* { dg-error "crug-SAFE-005" } */
}
