int sum(int n, int a[n])
{
  int total = 0;
  for (int i = 0; i < n; i++)
    total += a[i];
  return total;
}

int local_vla(int n)
{
  int a[n];
  for (int i = 0; i < n; i++)
    a[i] = i;
  return sum(n, a);
}
