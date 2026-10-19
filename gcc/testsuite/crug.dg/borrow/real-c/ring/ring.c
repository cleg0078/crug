/* A ring buffer of integers, with wrap-around.  */

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

struct Ring { int data[8]; unsigned long head; unsigned long tail; };

static int ring_push (mut struct Ring *r, int value)
{
  unsigned long next = (r->head + 1) % 8;
  if (next == r->tail)
    return 0;
  r->data[r->head] = value;
  r->head = next;
  return 1;
}

static int ring_pop (mut struct Ring *r, int *out)
{
  if (r->head == r->tail)
    return 0;
  *out = r->data[r->tail];
  r->tail = (r->tail + 1) % 8;
  return 1;
}

static unsigned long ring_size (ref struct Ring *r)
{
  return (r->head + 8 - r->tail) % 8;
}

int main (void)
{
  int value = 0;
  int total = 0;
  {
    own struct Ring *r = adopt (arena_alloc (sizeof *r), arena_free);
    r->head = 0;
    r->tail = 0;
    {
      mut struct Ring *edit = borrow_mut (r);
      int i;
      for (i = 0; i < 5; i++)
	if (!ring_push (edit, i + 1))
	  return 1;
    }
    {
      ref struct Ring *view = borrow (r);
      if (ring_size (view) != 5)
	return 2;
    }
    {
      mut struct Ring *edit = borrow_mut (r);
      if (!ring_pop (edit, &value))
	return 3;
      total += value;
      if (!ring_push (edit, 6))
	return 3;
      if (!ring_pop (edit, &value))
	return 3;
      total += value;
    }
    {
      ref struct Ring *view = borrow (r);
      if (ring_size (view) != 4)
	return 4;
    }
  }
  return total == 3 && destroyed == 1 ? 0 : 5;
}
