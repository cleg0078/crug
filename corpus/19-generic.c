#define kind(x) _Generic((x), int: 1, long: 2, double: 3, default: 0)

int classify(void)
{
  return kind(1) + kind(1L) + kind(1.0) + kind("s");
}
