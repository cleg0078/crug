/* Safe core types: a slice, option and result, with a match statement.
   Build and run:
     ./gcc/crugc -O2 -fcrug-slices -fcrug-result examples/11-safe-types.c \
       -o build/safe-types && ./build/safe-types   */

#include <stdio.h>

/* Returns some (sum) while it is small enough, none otherwise.  */
static option<int> add_all (slice<int> values)
{
  int total = 0;
  for (size_t i = 0; i < values.len; i++)
    total += values[i];
  if (total < 1000)
    return some (total);
  return none;
}

/* Splits an allocation into an ok value or an err value.  */
static result<int, int> take (option<int> current, int amount)
{
  match (current)
    {
    some (have) => {
      if (amount > have)
        return err (amount - have);
      return ok (have - amount);
    }
    none => { return err (amount); }
    }
}

int main (void)
{
  int a[4] = { 10, 20, 30, 40 };

  option<int> sum = add_all (slice (a, 4));
  match (sum)
    {
    some (total) => { printf ("total=%d\n", total); }
    none => { printf ("total=none\n"); }
    }

  result<int, int> left = take (sum, 90);
  match (left)
    {
    ok (rest) => { printf ("rest=%d\n", rest); }
    err (short_by) => { printf ("short by %d\n", short_by); }
    }

  result<int, int> too_much = take (sum, 1000);
  match (too_much)
    {
    ok (rest) => { printf ("rest=%d\n", rest); }
    err (short_by) => { printf ("short by %d\n", short_by); }
    }

  return 0;
}
