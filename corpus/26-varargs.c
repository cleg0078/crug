#include <stdarg.h>

int sum(int count, ...)
{
  va_list ap;
  int total = 0;
  va_start(ap, count);
  for (int i = 0; i < count; i++)
    total += va_arg(ap, int);
  va_end(ap);
  return total;
}
