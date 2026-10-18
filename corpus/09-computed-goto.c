int dispatch(int op)
{
  static void *table[] = { &&add, &&sub, &&mul };
  int r = 0;

  if (op < 0 || op > 2)
    return -1;
  goto *table[op];
add:
  r = 1;
  goto done;
sub:
  r = 2;
  goto done;
mul:
  r = 3;
done:
  return r;
}
