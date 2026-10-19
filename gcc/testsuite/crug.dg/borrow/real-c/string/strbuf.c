/* A fixed-capacity string buffer; appends go through a mutable borrow.  */

static char arena[65536];
static unsigned long arena_used;
static int destroyed;

static void *arena_alloc (unsigned long n)
{
  void *p = arena + arena_used;
  arena_used += (n + 15) & ~15UL;
  return p;
}

static void arena_free (void *p)
{
  (void) p;
  destroyed++;
}

struct Strbuf { char *data; unsigned long length; unsigned long capacity; };

static int strbuf_append (mut struct Strbuf *b, char c)
{
  if (b->length + 1 >= b->capacity)
    return 0;
  b->data[b->length++] = c;
  b->data[b->length] = 0;
  return 1;
}

static unsigned long strbuf_length (ref struct Strbuf *b)
{
  return b->length;
}

int main (void)
{
  {
    own struct Strbuf *b = adopt (arena_alloc (sizeof *b), arena_free);
    b->data = (char *) arena_alloc (16);
    b->length = 0;
    b->capacity = 16;
    b->data[0] = 0;
    {
      mut struct Strbuf *edit = borrow_mut (b);
      if (!strbuf_append (edit, 'c'))
	return 1;
      if (!strbuf_append (edit, 'r'))
	return 1;
      if (!strbuf_append (edit, 'u'))
	return 1;
      if (!strbuf_append (edit, 'g'))
	return 1;
    }
    {
      ref struct Strbuf *view = borrow (b);
      if (strbuf_length (view) != 4)
	return 2;
      if (view->data[0] != 'c' || view->data[3] != 'g')
	return 3;
    }
    if (b->capacity != 16)
      return 4;
  }
  return destroyed == 1 ? 0 : 5;
}
