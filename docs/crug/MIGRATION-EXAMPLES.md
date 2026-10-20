# Migrating from C to Crug: worked examples

Status: **Phase 20** (2026-10-20). This page has six before/after programs for
the constructs a C programmer meets first: heap ownership, owned parameters and
`move`, borrows, structured concurrency, `option`/`result`, and payload
enums. Every "after" program is compiled and run by
`scripts/check-docs-examples.sh`; the "before" program is plain C and is shown
for comparison.

The check reads this file, compiles each marked block with the compiler in this
tree, runs it, and compares stdout byte for byte with the expected block:

```sh
scripts/check-docs-examples.sh
```

A warm tree prints one summary line, for example
`docs-examples: 6 example(s), 0 failure(s)`, and exits 0. A mismatch is a
failure; a missing compiler or a malformed block is a setup error.

## 1. How the executable blocks are marked

An example starts with an HTML comment of the form
`<!-- example: NAME flags: FLAGS -->`, immediately followed by a `c` fenced
block. The block is compiled with those flags. After the closing fence comes
`<!-- expect -->` and a `text` fenced block holding the exact stdout. The
script passes the flags to `crugc` verbatim; a word in the flags that is not a
compiler option would therefore fail the compile and be reported as a failure,
so the flag list is the single source of truth.

The "before" blocks are not marked: they are ordinary C and are compiled with
`cc -O2` (the pthread baseline also needs `-pthread`). They are documentation,
not a second source of truth.

## 2. Heap allocation: `malloc`/`free` become `adopt` and `drop`

Before, the allocation and the release are two unrelated statements, and a
reader has to check that every exit path reaches the release.

```c
#include <stdio.h>
#include <stdlib.h>

struct point
{
  int x;
  int y;
};

static void
destroy_point (struct point *p)
{
  printf ("released %d,%d\n", p->x, p->y);
  free (p);
}

int
main (void)
{
  struct point *a = malloc (sizeof *a);
  a->x = 1;
  a->y = 2;
  printf ("point %d,%d\n", a->x, a->y);
  destroy_point (a);

  struct point *b = malloc (sizeof *b);
  b->x = 3;
  b->y = 4;
  printf ("point %d,%d\n", b->x, b->y);
  destroy_point (b);
  return 0;
}
```

In Crug the destructor is attached at adoption, `drop` destroys at a chosen
point, and `b` is destroyed automatically at the end of its scope in reverse
declaration order.

<!-- example: own-drop flags: -fcrug-ownership -->
```c
#include <stdio.h>
#include <stdlib.h>

struct point
{
  int x;
  int y;
};

static void
destroy_point (struct point *p)
{
  printf ("released %d,%d\n", p->x, p->y);
  free (p);
}

int
main (void)
{
  own struct point *a = adopt (malloc (sizeof *a), destroy_point);
  a->x = 1;
  a->y = 2;
  printf ("point %d,%d\n", a->x, a->y);
  drop (a);

  own struct point *b = adopt (malloc (sizeof *b), destroy_point);
  b->x = 3;
  b->y = 4;
  printf ("point %d,%d\n", b->x, b->y);
  return 0;
}
```
<!-- expect -->
```text
point 1,2
released 1,2
point 3,4
released 3,4
```

Compile it with `gcc/crugc -O2 -fcrug-ownership program.c -o program`. The
automatic destruction of `b` is the same destructor call as the explicit one;
the compiler inserts it on every exit path (ADR 0005, `DROP.md`).

## 3. Owned parameters and `move`

Before, the callee frees a pointer it does not own and the caller may still
hold a copy. Nothing in C prevents a second use.

```c
#include <stdio.h>
#include <stdlib.h>

struct node
{
  int value;
};

static void
consume (struct node *n)
{
  printf ("consume %d\n", n->value);
  printf ("released %d\n", n->value);
  free (n);
}

int
main (void)
{
  struct node *a = malloc (sizeof *a);
  a->value = 42;
  consume (a);
  printf ("caller done\n");
  return 0;
}
```

In Crug the parameter type says the callee owns the value, and `move` at the
call site transfers it. The destructor does not travel with the parameter, so
the callee adopts explicitly; the caller's `a` is moved and cannot be used or
released again.

<!-- example: move-param flags: -fcrug-ownership -->
```c
#include <stdio.h>
#include <stdlib.h>

struct node
{
  int value;
};

static void
destroy_node (struct node *n)
{
  printf ("released %d\n", n->value);
  free (n);
}

static void
consume (own struct node *n)
{
  own struct node *owned = adopt (n, destroy_node);
  printf ("consume %d\n", owned->value);
}

int
main (void)
{
  own struct node *a = adopt (malloc (sizeof *a), destroy_node);
  a->value = 42;
  consume (move (a));
  printf ("caller done\n");
  return 0;
}
```
<!-- expect -->
```text
consume 42
released 42
caller done
```

Compile it with `gcc/crugc -O2 -fcrug-ownership program.c -o program`. A use of
`a` after `move (a)` is `[crug-UAM-001]`, and dropping it there would be a
second destruction. `DROP.md` and `OWNERSHIP.md` state the exact rules.

## 4. Borrows and last-use lifetimes

Before, `const` on a pointer is a comment about the access path, not a checked
loan; the mutable and the shared aliases coexist for the whole function.

```c
#include <stdio.h>
#include <stdlib.h>

struct counter
{
  int value;
};

static void
destroy_counter (struct counter *c)
{
  printf ("released %d\n", c->value);
  free (c);
}

static int
read_counter (const struct counter *c)
{
  return c->value;
}

static void
set_counter (struct counter *c, int v)
{
  c->value = v;
}

int
main (void)
{
  struct counter *c = malloc (sizeof *c);
  c->value = 10;

  const struct counter *r = c;
  printf ("read %d\n", read_counter (r));

  set_counter (c, 11);
  printf ("now %d\n", c->value);

  struct counter *moved = c;
  printf ("moved %d\n", moved->value);
  destroy_counter (moved);
  return 0;
}
```

In Crug `borrow` and `borrow_mut` are checked loans. A shared borrow ends at
its last use (ADR 0020, non-lexical lifetimes), so the mutable borrow can start
after the last read, and the value can be moved after the mutable borrow ends.
The callee parameter types `ref` and `mut` state the same contract in a
prototype.

<!-- example: borrow-nll flags: -fcrug-borrow -->
```c
#include <stdio.h>
#include <stdlib.h>

struct counter
{
  int value;
};

static void
destroy_counter (struct counter *c)
{
  printf ("released %d\n", c->value);
  free (c);
}

static int
read_counter (ref struct counter *c)
{
  return c->value;
}

static void
set_counter (mut struct counter *c, int v)
{
  c->value = v;
}

int
main (void)
{
  own struct counter *c = adopt (malloc (sizeof *c), destroy_counter);
  c->value = 10;

  ref struct counter *r = borrow (c);
  printf ("read %d\n", read_counter (r));

  mut struct counter *m = borrow_mut (c);
  set_counter (m, 11);
  printf ("now %d\n", m->value);

  own struct counter *moved = move (c);
  printf ("moved %d\n", moved->value);
  return 0;
}
```
<!-- expect -->
```text
read 10
now 11
moved 11
released 11
```

Compile it with `gcc/crugc -O2 -fcrug-borrow program.c -o program`. The borrow
dump records where each loan begins and ends:
`gcc/crugc -fcrug-borrow -fcrug-dump=borrow -c program.c -o program.o` writes
`program.crug-borrow` next to the object. See `BORROW.md` section 4.

## 5. Structured scope, tasks, and channels

Before, pthread creation and joining are separate calls, a shared array needs
external synchronisation, and the handoff buffer is a global.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static int results[4];

struct square_arg
{
  int index;
  int value;
};

static void *
square (void *p)
{
  struct square_arg *a = p;
  results[a->index] = a->value * a->value;
  return NULL;
}

static char *handoff;

static void *
produce (void *p)
{
  (void) p;
  handoff = malloc (8);
  strcpy (handoff, "hello");
  return NULL;
}

int
main (void)
{
  pthread_t workers[4];
  struct square_arg args[4];

  for (int i = 0; i < 4; i++)
    {
      args[i].index = i;
      args[i].value = i + 1;
      pthread_create (&workers[i], NULL, square, &args[i]);
    }
  for (int i = 0; i < 4; i++)
    pthread_join (workers[i], NULL);

  int sum = 0;
  for (int i = 0; i < 4; i++)
    sum += results[i];

  int left = 7;
  int picked = left;

  pthread_t producer;
  pthread_create (&producer, NULL, produce, NULL);
  pthread_join (producer, NULL);

  printf ("sum=%d\n", sum);
  printf ("picked=%d\n", picked);
  printf ("text=%s\n", handoff);
  free (handoff);
  return 0;
}
```

In Crug `scope` waits for every task it starts, the channel carries the
results, and an owning element is transferred with `move`. The receiver of
`chan own char *` owns the buffer but has no destructor for it, so it calls
`release` after use.

<!-- example: scope-chan flags: -fcrug-channels -fcrug-ownership -->
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
send_square (chan int out, int v)
{
  send (out, v * v);
}

static void
send_text (chan own char *out)
{
  own char *text = adopt (malloc (8), free);
  strcpy (text, "hello");
  send (out, move (text));
}

int
main (void)
{
  chan int results = channel (8);
  scope {
    for (int i = 1; i <= 4; i++)
      go send_square (results, i);
  }

  int sum = 0;
  for (int i = 0; i < 4; i++)
    sum += recv (results);

  chan int left = channel (1);
  chan int right = channel (1);
  send (left, 7);
  int picked = 0;
  select {
  case v = recv (left):
    picked = v;
  case w = recv (right):
    picked = w;
  }

  chan own char *texts = channel (1);
  scope {
    go send_text (texts);
  }
  own char *text = recv (texts);

  printf ("sum=%d\n", sum);
  printf ("picked=%d\n", picked);
  printf ("text=%s\n", text);
  release (text);
  return 0;
}
```
<!-- expect -->
```text
sum=30
picked=7
text=hello
```

This is a runtime example. Build the Linux runtime objects once and link them;
the flags, include directory and object list are the ones in
`scripts/build-examples.sh`:

```sh
make -C libcrug PAL=linux
gcc/crugc -O2 -fcrug-channels -fcrug-ownership -Ilibcrug/core program.c \
  libcrug/build/linux/task.o libcrug/build/linux/pal_linux.o \
  libcrug/build/linux/switch.o libcrug/build/linux/stack.o \
  libcrug/build/linux/chan.o -pthread -o program
```

`scope` waits at the closing brace, including for the nested `scope`. The
`select` chooses the only ready receive (`left`) and leaves `right` empty.
`SCOPE.md` and `CHANNELS.md` give the lifetime, Send/Sync and ownership rules.

## 6. `option`, `result`, and `match`

Before, a tag field and a sentinel value encode absence and failure, and the
caller must remember which branch is legal.

```c
#include <stdio.h>

struct option_int
{
  int is_some;
  int value;
};

static struct option_int
checked (int v)
{
  struct option_int o;
  o.is_some = v >= 0;
  o.value = v >= 0 ? v : 0;
  return o;
}

struct result_int
{
  int is_ok;
  int value;
  int error;
};

static struct result_int
divide (int a, int b)
{
  struct result_int r;
  r.is_ok = b != 0;
  r.value = b != 0 ? a / b : 0;
  r.error = b != 0 ? 0 : -1;
  return r;
}

int
main (void)
{
  struct option_int yes = checked (7);
  struct option_int no = checked (-1);

  if (yes.is_some)
    printf ("some %d\n", yes.value);
  else
    printf ("none\n");

  if (no.is_some)
    printf ("some %d\n", no.value);
  else
    printf ("none\n");

  struct result_int good = divide (8, 2);
  struct result_int bad = divide (8, 0);

  if (good.is_ok)
    printf ("ok %d\n", good.value);
  else
    printf ("err %d\n", good.error);

  if (bad.is_ok)
    printf ("ok %d\n", bad.value);
  else
    printf ("err %d\n", bad.error);

  return 0;
}
```

In Crug `option<T>` and `result<T,E>` carry the case in the type, and
`match` is exhaustive. The constructors are only legal in a declaration
initializer or a `return` (ADR 0018; `SAFE-TYPES.md`).

<!-- example: option-result flags: -fcrug-result -->
```c
#include <stdio.h>

static option<int>
checked (int v)
{
  if (v >= 0)
    return some (v);
  return none;
}

static result<int, int>
divide (int a, int b)
{
  if (b == 0)
    return err (-1);
  return ok (a / b);
}

int
main (void)
{
  option<int> yes = checked (7);
  option<int> no = checked (-1);

  match (yes)
    {
    some (v) => { printf ("some %d\n", v); }
    none => { printf ("none\n"); }
    }

  match (no)
    {
    some (v) => { printf ("some %d\n", v); }
    none => { printf ("none\n"); }
    }

  result<int, int> good = divide (8, 2);
  result<int, int> bad = divide (8, 0);

  match (good)
    {
    ok (v) => { printf ("ok %d\n", v); }
    err (e) => { printf ("err %d\n", e); }
    }

  match (bad)
    {
    ok (v) => { printf ("ok %d\n", v); }
    err (e) => { printf ("err %d\n", e); }
    }

  return 0;
}
```
<!-- expect -->
```text
some 7
none
ok 4
err -1
```

Compile it with `gcc/crugc -O2 -fcrug-result program.c -o program`.
A `match` that omits `none` or `err` is rejected at compile time; the payload
name in the arm is a new binding for the arm's block.

## 7. Payload enums

Before, a tag, a union, and a switch have to agree by convention. Nothing checks
that the active union member matches the tag.

```c
#include <stdio.h>
#include <string.h>

enum message_tag { TEXT, NUMBER, QUIT };

struct message
{
  enum message_tag tag;
  union
  {
    char *text;
    int number;
  } u;
};

static struct message
parse (const char *word)
{
  struct message m;
  if (strcmp (word, "quit") == 0)
    {
      m.tag = QUIT;
    }
  else if (word[0] >= '0' && word[0] <= '9')
    {
      m.tag = NUMBER;
      m.u.number = word[0] - '0';
    }
  else
    {
      m.tag = TEXT;
      m.u.text = (char *) word;
    }
  return m;
}

static void
handle (struct message m)
{
  switch (m.tag)
    {
    case TEXT:
      printf ("text=%s\n", m.u.text);
      break;
    case NUMBER:
      printf ("number+1=%d\n", m.u.number + 1);
      break;
    case QUIT:
      printf ("quit\n");
      break;
    }
}

int
main (void)
{
  handle (parse ("hello"));
  handle (parse ("7"));
  handle (parse ("quit"));
  return 0;
}
```

The Crug payload enum names the type of each payload in the declaration, and
`match` binds it. A unit variant such as `Quit` is written bare, both when
constructed and in a pattern.

<!-- example: payload-enum flags: -fcrug-adt -->
```c
#include <stdio.h>
#include <string.h>

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

int
main (void)
{
  handle (parse ("hello"));
  handle (parse ("7"));
  handle (parse ("quit"));
  return 0;
}
```
<!-- expect -->
```text
text=hello
number+1=8
quit
```

Compile it with `gcc/crugc -O2 -fcrug-adt program.c -o program`. The payload
of `Text` is a plain pointer in this example; an `own` payload transfers
the buffer and is matched with `match (move (m))` (see `SAFE-TYPES.md` and
`examples/14-owned-payload.c`).

## 8. What the examples do not show

The examples are deliberately small. They do not cover failure paths and
`goto` out of a scope, aggregate owning fields, slices and bounds checks,
strict mode, the FFI sidecar, or kernel mode; each has a dedicated document in
this directory. `examples/` has the full accepted programs, including the two
that are expected to fail with a documented code, and
`scripts/build-examples.sh` builds them.
