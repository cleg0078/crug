long double scale(long double x, int n)
{
  long double r = x;
  for (int i = 0; i < n; i++)
    r *= 1.5L;
  return r;
}

double narrow(long double x) { return (double)x; }
