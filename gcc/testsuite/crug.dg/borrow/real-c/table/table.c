/* An open-addressing table of small integers.  */

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

struct Table { int keys[32]; int values[32]; unsigned long used; };

static unsigned long slot_for (int key)
{
  return (unsigned long) (key & 31);
}

static int table_insert (mut struct Table *t, int key, int value)
{
  unsigned long i = slot_for (key);
  unsigned long probes = 0;
  while (probes < 32)
    {
      if (t->keys[i] == 0)
	{
	  t->keys[i] = key;
	  t->values[i] = value;
	  t->used++;
	  return 1;
	}
      i = (i + 1) & 31;
      probes++;
    }
  return 0;
}

static int table_lookup (ref struct Table *t, int key, int *out)
{
  unsigned long i = slot_for (key);
  unsigned long probes = 0;
  while (probes < 32)
    {
      if (t->keys[i] == key)
	{
	  *out = t->values[i];
	  return 1;
	}
      if (t->keys[i] == 0)
	return 0;
      i = (i + 1) & 31;
      probes++;
    }
  return 0;
}

int main (void)
{
  int value = 0;
  {
    own struct Table *t = adopt (arena_alloc (sizeof *t), arena_free);
    unsigned long i;
    for (i = 0; i < 32; i++)
      {
	t->keys[i] = 0;
	t->values[i] = 0;
      }
    t->used = 0;
    {
      mut struct Table *edit = borrow_mut (t);
      if (!table_insert (edit, 1, 10))
	return 1;
      if (!table_insert (edit, 33, 11))
	return 1;
      if (!table_insert (edit, 7, 12))
	return 1;
    }
    {
      ref struct Table *view = borrow (t);
      if (!table_lookup (view, 33, &value))
	return 2;
      if (value != 11)
	return 3;
      if (table_lookup (view, 5, &value))
	return 4;
      if (view->used != 3)
	return 5;
    }
  }
  return destroyed == 1 ? 0 : 6;
}
