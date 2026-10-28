/* Spaced argument references: `arg 0` and a zero-padded index both name
   parameter 0 (CR-01, 0.2.4).  The functions are only declared, so the case
   needs nothing from the phase9 library.  */

void spaced_free (void *);
void padded_free (void *);

int
main (void)
{
  return 0;
}
