/* pal_guard_fault.c - prove that a claimed guard page actually faults.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The contract suite can only assert that crug_pal_memory_protect_guard()
   reports success; a guard page that does not fault would still pass that
   assertion.  This test therefore writes into a guarded range inside a child
   process and requires the child to die of the fault.  It lives outside
   libcrug/core because it uses process control, which the core must never do
   (ADR 0011 D7).
*/

#include "crug_pal.h"

#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

int
main (void)
{
  const size_t page = (size_t) 1 << 16;
  const size_t size = page * 2;
  unsigned char *base = (unsigned char *) crug_pal_memory_reserve (size);
  pid_t child;
  int status;

  if (base == NULL)
    {
      printf ("guard-fault: FAIL (reserve)\n");
      return 1;
    }
  if (!crug_pal_memory_commit (base, page))
    {
      printf ("guard-fault: FAIL (commit)\n");
      return 1;
    }
  if (!crug_pal_memory_protect_guard (base + page, page))
    {
      crug_pal_memory_release (base, size);
      printf ("guard-fault: FAIL (the Linux backend must support guard pages)\n");
      return 1;
    }
  child = fork ();
  if (child < 0)
    {
      printf ("guard-fault: FAIL (fork)\n");
      return 1;
    }
  if (child == 0)
    {
      volatile unsigned char *guard = base + page;
      *guard = 1;  /* must fault */
      _exit (0);
    }
  if (waitpid (child, &status, 0) < 0)
    {
      printf ("guard-fault: FAIL (waitpid)\n");
      return 1;
    }
  crug_pal_memory_release (base, size);
  if (!WIFSIGNALED (status) || WTERMSIG (status) != SIGSEGV)
    {
      printf ("guard-fault: FAIL (the guarded page did not fault)\n");
      return 1;
    }
  printf ("guard-fault: SIGSEGV on the guarded page as claimed\n");
  return 0;
}
