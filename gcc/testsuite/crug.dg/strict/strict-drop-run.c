/* { dg-do run } */
/* { dg-options "-O2 -fcrug-strict" } */
static int destroyed;
struct Foo { int v; };

static void destroy (struct Foo *p)
{
  destroyed++;
  (void) p;
}

static struct Foo *source (void)
{
  static struct Foo f = { 9 };
  return &f;
}

int main (void)
{
  {
    own struct Foo *a = adopt (source (), destroy);
    if (a->v != 9)
      return 1;
  }
  return destroyed == 1 ? 0 : 1;
}
