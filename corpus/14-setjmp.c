#include <setjmp.h>

jmp_buf env;

int try(int value)
{
  if (setjmp(env) != 0)
    return -1;
  if (value < 0)
    longjmp(env, 1);
  return value;
}
