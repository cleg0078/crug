/* Goroutines: 'go' copies a per-call-site frame and one spawn call queues
   the task on a worker thread; 'yield' hands the worker over; an owned value
   crosses the task boundary only through move(), and the task destroys it.
   Build (the runtime objects come from the libcrug build):
     make -C libcrug PAL=linux
     ./gcc/crugc -O2 -fcrug-goroutines -fcrug-ownership -Ilibcrug/core \
       examples/16-goroutines.c libcrug/build/linux/task.o \
       libcrug/build/linux/pal_linux.o libcrug/build/linux/switch.o \
       libcrug/build/linux/stack.o -pthread -o build/ex-goroutines
     ./build/ex-goroutines     */

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#include "task.h" /* crug_task_stats_get, crug_task_runtime_shutdown */

#define TASKS 8

static _Atomic int total;
static _Atomic int released;

/* A plain by-value capture: no ownership involved.  */
static void
add (int v)
{
  atomic_fetch_add (&total, v);
}

/* An own parameter: the task owns the buffer and releases it itself, so the
   spawner transfers it with move() and never touches it again.  */
static void
tick (own char *p)
{
  yield (); /* let another task run before this one finishes */
  atomic_fetch_add (&released, 1);
  release (p);
}

int
main (void)
{
  static int values[TASKS] = { 1, 2, 3, 4, 5, 6, 7, 8 };

  for (int i = 0; i < TASKS; i++)
    go add (values[i]);

  /* The indirect form: any call expression is accepted.  */
  void (*fn) (int) = add;
  go (fn (99));

  /* Two tasks take ownership of a heap buffer each.  */
  for (int i = 0; i < 2; i++)
    {
      own char *buf = (char *) malloc (4);
      go tick (move (buf));
    }

  yield (); /* no-op on the main thread: the loop above already spawned */

  crug_task_runtime_shutdown (); /* run what is left, join the workers */

  struct crug_task_stats s;
  crug_task_stats_get (&s);
  printf ("spawned=%llu completed=%llu lost=%llu workers=%u\n",
	  (unsigned long long) s.spawned, (unsigned long long) s.completed,
	  (unsigned long long) s.lost, s.workers);
  printf ("total=%d released=%d\n", atomic_load (&total),
	  atomic_load (&released));

  /* 1+2+...+8 = 36, plus the indirect call's 99, and two buffers released.  */
  return atomic_load (&total) == 135 && atomic_load (&released) == 2
	 && s.lost == 0 ? 0 : 1;
}
