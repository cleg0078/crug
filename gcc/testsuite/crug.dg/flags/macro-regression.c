/* Crug words stay usable as macro names.
 *
 * A project that defines own() or move() keeps its macro: the intrinsic
 * reading requires the name to be an undeclared identifier (ADR 0018 D3), and
 * the qualifier reading is a declaration-specifier lookahead (D2).  This file
 * must compile with -fcrug-modern and with -fcrug-kernel, and with no flags.
 */
#define own(x) (x)
#define move(x) (x)
#define go(label) goto label
#define chan int

static int
use_macros (void)
{
  int value = own (3);
  value = move (value);
  go (done);
  return value;
 done:
  return 0;
}

int
macro_regression (void)
{
  return use_macros ();
}
