/* Owned payloads: an enum variant whose payload owns memory is transferred
   into the constructor, dropped only when its variant is active, and moved
   into match, where the binding owns it for the arm.
   Build and run:
     ./gcc/crugc -O2 -fcrug-adt -fcrug-ownership examples/14-owned-payload.c \
       -o build/owned-payload && ./build/owned-payload   */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int frees;

static char *
mk_text (const char *s)
{
  size_t n = strlen (s) + 1;
  char *p = (char *) malloc (n);
  memcpy (p, s, n);
  return p;
}

static void
release_text (char *p)
{
  frees++;
  free (p);
}

/* Text owns its payload; Number and Quit own nothing.  The layout is an
   ordinary record either way: an 'own' payload is analysis metadata only.  */
enum Message
{
  Text (own char *),
  Number (int),
  Quit,
};

int
main (void)
{
  /* Ownership moves into the value: adopt says who releases the text.  */
  enum Message greeting = Text (adopt (mk_text ("hello"), release_text));
  enum Message count = Number (42);

  /* An owned scrutinee must be moved into match (crug-ADT-015), and the arm
     that covers Text must bind the payload (crug-ADT-020).  The binding owns
     the text and releases it at the end of the arm.  */
  match (move (greeting))
    {
    Text (text) => { printf ("text=%s\n", text); }
    Number (n) => { printf ("number=%d\n", n); }
    Quit => { printf ("quit\n"); }
    }

  /* Number owns nothing, so this value drops nothing when it goes away.  */
  match (move (count))
    {
    Text (text) => { printf ("text=%s\n", text); }
    Number (n) => { printf ("number=%d\n", n); }
    Quit => { printf ("quit\n"); }
    }

  enum Message quiet = Quit;
  match (move (quiet))
    {
    Text (text) => { printf ("text=%s\n", text); }
    Number (n) => { printf ("number=%d\n", n); }
    Quit => { printf ("quit\n"); }
    }

  /* Exactly one release: only the Text value ever owned anything.  */
  printf ("frees=%d\n", frees);
  return frees == 1 ? 0 : 1;
}
