/* { dg-do run } */
#include "contracts.h"
static int called;
static void handler (void) { called = 1; }
int main (void)
{
  run_cb (0);
  run_cb (handler);
  return called == 1 ? 0 : 1;
}
