/* crug-CONC-015: a goto that jumps into a scope body.  */
static void work (void)
{
}

int main (void)
{
  goto inside;
  scope {
  inside:
    go work ();
  }
  return 0;
}
