/* crug-CONC-024: an array argument decays to a pointer, which is not Send.  */
static void tick (void)
{
}

static void fill (int *p, int n)
{
  for (int i = 0; i < n; i++)
    p[i] = i;
}

int main (void)
{
  int buf[4];

  go tick ();
  go fill (buf, 4);
  return buf[0];
}
