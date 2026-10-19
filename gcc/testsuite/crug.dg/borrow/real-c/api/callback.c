/* A tiny event dispatcher: a handler receives a mutable borrow of the state
   it updates, and the dispatcher seeds it from a shared borrow.  */

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

struct Counters { int events; int handled; };

static void on_event (mut struct Counters *c, int weight)
{
  c->handled += weight;
}

static int total_events (ref struct Counters *c)
{
  return c->events;
}

static void dispatch (mut struct Counters *c, int events)
{
  int i;
  c->events = events;
  for (i = 0; i < events; i++)
    on_event (c, i + 1);
}

int main (void)
{
  {
    own struct Counters *c = adopt (arena_alloc (sizeof *c), arena_free);
    c->events = 0;
    c->handled = 0;
    {
      mut struct Counters *edit = borrow_mut (c);
      dispatch (edit, 4);
    }
    {
      ref struct Counters *view = borrow (c);
      if (total_events (view) != 4)
	return 1;
    }
    if (c->handled != 10)
      return 2;
  }
  return destroyed == 1 ? 0 : 3;
}
