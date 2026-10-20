/* crug-CONC-004: a select arm must be case v = recv (ch) or send form.  */
int main (void)
{
  chan int a = channel (1);

  select {
    case x + 1:
      ;
  }
  return 0;
}
