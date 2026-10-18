static void drop_int(int *p) { *p = 0; }

int scoped(int n)
{
  int value __attribute__((cleanup(drop_int))) = n;
  return value;
}
