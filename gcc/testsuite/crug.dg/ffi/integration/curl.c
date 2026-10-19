/* libcurl: curl_easy_init allocates, curl_easy_cleanup frees.  */

#include <stdio.h>


typedef void CURL;

extern CURL *curl_easy_init (void);
extern void curl_easy_cleanup (CURL *easy);

static int inits;
static int cleanups;

CURL *my_init (void)
{
  CURL *e = curl_easy_init ();
  if (e != 0)
    inits++;
  return e;
}

void my_cleanup (CURL *e)
{
  if (e == 0)
    return;
  cleanups++;
  curl_easy_cleanup (e);
}

int main (void)
{
  int ok;
  {
    own CURL *e = my_init ();
    ok = (e != 0);
  }
  if (!ok)
    return 1;
  printf ("ok curl inits=%d cleanups=%d\n", inits, cleanups);
  return (inits == 1 && cleanups == 1) ? 0 : 2;
}
