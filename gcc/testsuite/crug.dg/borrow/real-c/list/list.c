/* A singly linked list built from an arena, driven through borrows.  */

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

struct Node { int value; struct Node *next; };
struct List { struct Node *head; unsigned long length; };

static void list_push (mut struct List *l, int value)
{
  own struct Node *n = adopt (arena_alloc (sizeof *n), arena_free);
  n->value = value;
  n->next = l->head;
  l->head = release (n);
  l->length++;
}

static int list_sum (ref struct List *l)
{
  int total = 0;
  struct Node *n = l->head;
  while (n != (struct Node *) 0)
    {
      total += n->value;
      n = n->next;
    }
  return total;
}

static void list_scale (mut struct List *l, int factor)
{
  struct Node *n = l->head;
  while (n != (struct Node *) 0)
    {
      n->value *= factor;
      n = n->next;
    }
}

int main (void)
{
  struct List list;
  list.head = (struct Node *) 0;
  list.length = 0;
  {
    own struct List *l = adopt (arena_alloc (sizeof *l), arena_free);
    l->head = (struct Node *) 0;
    l->length = 0;
    list_push (l, 1);
    list_push (l, 2);
    list_push (l, 3);
    {
      ref struct List *view = borrow (l);
      if (list_sum (view) != 6)
	return 1;
      if (view->length != 3)
	return 2;
    }
    {
      mut struct List *edit = borrow_mut (l);
      list_scale (edit, 10);
    }
    {
      ref struct List *view = borrow (l);
      if (list_sum (view) != 60)
	return 3;
    }
    list = *l;
  }
  return list.length == 3 ? 0 : 4;
}
