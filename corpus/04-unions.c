union number { int i; long l; double d; unsigned char bytes[8]; };

long as_long(union number n) { return n.l; }

union number from_int(int i)
{
  union number n;
  n.i = i;
  return n;
}
