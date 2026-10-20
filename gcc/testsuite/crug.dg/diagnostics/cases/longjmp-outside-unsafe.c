/* crug-SAFE-005: setjmp/longjmp outside unsafe.  */
typedef struct { int v[8]; } jmp_buf;
extern int setjmp (jmp_buf *);
extern void longjmp (jmp_buf *, int);

jmp_buf env;

void jump (void)
{
  longjmp (&env, 1);
}
