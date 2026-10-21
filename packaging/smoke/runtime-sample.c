/* runtime-sample.c - go, channels and the task runtime.
 *
 *   crugc -O2 -fcrug-goroutines -fcrug-channels -I/opt/crug/include/crug \
 *     runtime-sample.c -L/opt/crug/lib -lcrug -pthread -o runtime-sample
 *
 * A scope starts three tasks that send one number each on a buffered channel;
 * the block waits for the children, then main receives the three values and
 * prints their sum.  The runtime shutdown at the end is what joins the worker
 * threads, so the program leaves no task behind.
 */

#include <stdio.h>

#include "task.h"
#include "chan.h"

static void
send_value (chan int out, int value)
{
  send (out, value);
}

int
main (void)
{
  chan int numbers = channel (4);

  scope
  {
    go send_value (numbers, 1);
    go send_value (numbers, 2);
    go send_value (numbers, 4);
  }

  int sum = 0;
  for (int i = 0; i < 3; i++)
    sum += recv (numbers);

  crug_task_runtime_shutdown ();

  printf ("runtime-ok sum=%d\n", sum);
  return sum == 7 ? 0 : 1;
}
