/* A tokenizer over a fixed string; the cursor is advanced through a mutable
   borrow of the state.  */

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

struct Cursor { const char *text; unsigned long pos; };
struct Token { unsigned long start; unsigned long length; int kind; };

static int is_space (char c)
{
  return c == ' ' || c == '\t';
}

static int is_digit (char c)
{
  return c >= '0' && c <= '9';
}

static int next_token (mut struct Cursor *c, struct Token *out)
{
  const char *text = c->text;
  while (is_space (text[c->pos]))
    c->pos++;
  if (text[c->pos] == 0)
    return 0;
  out->start = c->pos;
  if (is_digit (text[c->pos]))
    {
      out->kind = 1;
      while (is_digit (text[c->pos]))
	c->pos++;
    }
  else
    {
      out->kind = 2;
      while (text[c->pos] != 0 && !is_space (text[c->pos])
	     && !is_digit (text[c->pos]))
	c->pos++;
    }
  out->length = c->pos - out->start;
  return 1;
}

int main (void)
{
  int numbers = 0;
  int words = 0;
  {
    own struct Cursor *c = adopt (arena_alloc (sizeof *c), arena_free);
    struct Token token;
    c->text = "ab 12 cd 345 e";
    c->pos = 0;
    {
      mut struct Cursor *edit = borrow_mut (c);
      while (next_token (edit, &token))
	{
	  if (token.kind == 1)
	    numbers++;
	  else
	    words++;
	}
    }
    {
      ref struct Cursor *view = borrow (c);
      if (view->pos != 14)
	return 1;
    }
  }
  if (destroyed != 1)
    return 2;
  return numbers == 2 && words == 3 ? 0 : 3;
}
