/* Plain C compiled by the Crug driver.
 *
 * No -fcrug-* flag is passed, so this translation unit is ordinary C: the
 * driver is argument-transparent (docs/crug/DRIVER.md).
 */

#include <stdio.h>

int
main (void)
{
  printf ("hello from crugc\n");
  return 0;
}