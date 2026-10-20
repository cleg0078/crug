/* crug-CONC-007: a select arm uses '=', not '=='.  */
int main (void)
{
  chan int a = channel (1);

  select {
    case v == recv (a):
      (void) v;
  }
  return 0;
}
