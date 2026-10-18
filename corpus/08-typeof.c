#define SWAP(a, b) do { __typeof__(a) t = (a); (a) = (b); (b) = t; } while (0)

int swap_ints(int x, int y)
{
  SWAP(x, y);
  return x - y;
}
