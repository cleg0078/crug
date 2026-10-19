/* Payload enums: an enum whose variants carry a value, matched with match.
   Build and run:
     ./gcc/crugc -O2 -fcrug-adt examples/13-payload-enum.c \
       -o build/payload-enum && ./build/payload-enum   */

#include <stdio.h>
#include <string.h>

/* A variant with a parenthesised payload makes this a payload enum: a tag plus
   a union of the payloads, laid out like an ordinary C record.  A variant
   without a payload (Quit) is constructed as the bare enumerator.  */
enum Message
{
  Text (char *),
  Number (int),
  Quit,
};

static enum Message
parse (const char *word)
{
  if (strcmp (word, "quit") == 0)
    return Quit;
  if (word[0] >= '0' && word[0] <= '9')
    return Number (word[0] - '0');
  return Text ((char *) word);
}

static void
handle (enum Message m)
{
  match (m)
    {
    Text (text) => { printf ("text=%s\n", text); }
    Number (n) => { printf ("number+1=%d\n", n + 1); }
    Quit => { printf ("quit\n"); }
    }
}

int main (void)
{
  handle (parse ("hello"));
  handle (parse ("7"));
  handle (parse ("quit"));

  enum Message q = Quit;
  printf ("size=%zu align=%zu\n", sizeof q, (size_t) _Alignof (enum Message));
  return 0;
}
