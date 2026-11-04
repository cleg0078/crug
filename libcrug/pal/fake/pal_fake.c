/* pal_fake.c - the contract-check backend of the Crug PAL.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Built with -DCRUG_PAL_FAKE.  It touches no operating system at all: threads
   run to completion inside join, wakeups only record their address, memory
   comes from the C allocator and guard pages are declined.  It exists so the
   core and the contract suite can be built and run on a machine that has no
   POSIX layer, and so the suite proves it depends on the PAL and nothing
   else (ADR 0011 D6).
*/

#include "crug_pal.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CRUG_PAL_FAKE
#define CRUG_PAL_FAKE_BUILD 1
#endif

static _Thread_local crug_pal_error fake_error = CRUG_PAL_OK;

/* Thread-exit hooks (contract 1.3).  The fake has no threads and no exit
   notification, so it models both: a hook registered while a fake thread's
   entry runs fires the moment that entry returns inside crug_pal_thread_join
   (the join-then-observe order a real backend gives), and a hook registered
   outside any fake entry stays armed until crug_pal_shutdown drains it at the
   end of the run (process teardown).  Registrations per thread are a small
   LIFO chain in thread-local storage, so the code is pure C: no operating
   system symbol appears anywhere in this file.  */

struct crug_pal_at_exit
{
  void (*fn) (void *);
  void *arg;
  struct crug_pal_at_exit *next;
};

static _Thread_local struct crug_pal_at_exit *fake_at_exit;

static void
fake_run_at_exit (void)
{
  struct crug_pal_at_exit *node = fake_at_exit;

  fake_at_exit = NULL;
  while (node != NULL)
    {
      struct crug_pal_at_exit *next = node->next;
      node->fn (node->arg);
      free (node);
      node = next;
    }
}

int
crug_pal_thread_at_exit (void (*fn) (void *), void *arg)
{
  struct crug_pal_at_exit *node;

  if (fn == NULL)
    {
      fake_error = CRUG_PAL_EINVAL;
      return -1;
    }
  node = (struct crug_pal_at_exit *) malloc (sizeof *node);
  if (node == NULL)
    {
      fake_error = CRUG_PAL_ENOMEM;
      return -1;
    }
  node->fn = fn;
  node->arg = arg;
  node->next = fake_at_exit;
  fake_at_exit = node;
  fake_error = CRUG_PAL_OK;
  return 0;
}

static uint32_t fake_waits;
static uint32_t fake_wakes;
static uint32_t fake_threads;

const char *
crug_pal_error_name (crug_pal_error error)
{
  switch (error)
    {
    case CRUG_PAL_OK: return "ok";
    case CRUG_PAL_EINVAL: return "invalid";
    case CRUG_PAL_ENOMEM: return "out of memory";
    case CRUG_PAL_EAGAIN: return "retry";
    case CRUG_PAL_ENOSYS: return "unsupported";
    case CRUG_PAL_ESYS: return "system";
    }
  return "unknown";
}

crug_pal_error
crug_pal_last_error (void)
{
  return fake_error;
}

void
crug_pal_clear_error (void)
{
  fake_error = CRUG_PAL_OK;
}

void
crug_pal_shutdown (void)
{
  /* Model process teardown: a hook on a thread that never exits still runs
     (contract 1.3).  */
  fake_run_at_exit ();
  fake_error = CRUG_PAL_OK;
}

struct crug_pal_thread
{
  void (*entry) (void *);
  void *arg;
  int ran;
};

int
crug_pal_thread_create (crug_pal_thread **out, void (*entry) (void *), void *arg)
{
  crug_pal_thread *thread;

  if (out == NULL || entry == NULL)
    {
      fake_error = CRUG_PAL_EINVAL;
      return -1;
    }
  thread = (crug_pal_thread *) malloc (sizeof *thread);
  if (thread == NULL)
    {
      fake_error = CRUG_PAL_ENOMEM;
      return -1;
    }
  thread->entry = entry;
  thread->arg = arg;
  thread->ran = 0;
  *out = thread;
  fake_threads++;
  fake_error = CRUG_PAL_OK;
  return 0;
}

int
crug_pal_thread_join (crug_pal_thread *thread)
{
  if (thread == NULL)
    {
      fake_error = CRUG_PAL_EINVAL;
      return -1;
    }
  if (!thread->ran)
    {
      thread->ran = 1;
      thread->entry (thread->arg);
      /* The entry returned: this thread is on its way out, so its at-exit
	 hooks (contract 1.3) run here, before the join can report success.  */
      fake_run_at_exit ();
    }
  /* CRUG_FAKE_JOIN_FAIL models a backend that cannot join: the entry has run,
     but the handle is not released, so the core must report the failure and
     keep its own state instead of pretending the worker was joined.  */
  if (getenv ("CRUG_FAKE_JOIN_FAIL") != NULL)
    {
      fake_error = CRUG_PAL_ESYS;
      return 1;
    }
  free (thread);
  fake_error = CRUG_PAL_OK;
  return 0;
}

uint32_t
crug_pal_cpu_count (void)
{
  return 1;
}

void
crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected)
{
  if (addr == NULL)
    return;
  fake_waits++;
  /* A fake backend cannot block: it reports the wait and returns.  The suite
     is written so that this is a legal result of the contract (the predicate
     is inspected, and the caller loops).  */
  (void) expected;
  (void) atomic_load_explicit (addr, memory_order_acquire);
}

void
crug_pal_wake_one (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  fake_wakes++;
}

void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  fake_wakes++;
}

size_t
crug_pal_page_size (void)
{
  /* The fake backend has no operating system, so it has no page granularity
     either; 4096 is the smallest page size the contract allows.  */
  return (size_t) 4096;
}

void *
crug_pal_memory_reserve (size_t size)
{
  void *ptr;

  if (size == 0)
    {
      fake_error = CRUG_PAL_EINVAL;
      return NULL;
    }
  ptr = malloc (size);
  if (ptr == NULL)
    {
      fake_error = CRUG_PAL_ENOMEM;
      return NULL;
    }
  memset (ptr, 0, size);
  fake_error = CRUG_PAL_OK;
  return ptr;
}

bool
crug_pal_memory_commit (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      fake_error = CRUG_PAL_EINVAL;
      return false;
    }
  fake_error = CRUG_PAL_OK;
  return true;
}

bool
crug_pal_memory_protect_guard (void *ptr, size_t size)
{
  (void) ptr;
  (void) size;
  /* Declining a capability is not an error (see the header contract).  */
  fake_error = CRUG_PAL_OK;
  return false;
}

void
crug_pal_memory_release (void *ptr, size_t size)
{
  (void) size;
  free (ptr);
}

/* The fake runtime reads this clock from several threads, so the advance is an
   atomic read-modify-write (RT-06): a plain increment races under TSan and can
   lose a tick even when the reader only needs monotonicity.  */
static _Atomic uint64_t fake_clock_ns;

uint64_t
crug_pal_clock_monotonic_ns (void)
{
  /* Monotonic by construction: the fake clock advances one microsecond per
     reading, never depends on wall time, and a sleep advances it by exactly
     the requested amount so duration checks have a defined answer.  */
  return atomic_fetch_add_explicit (&fake_clock_ns, 1000,
				    memory_order_relaxed) + 1000;
}

void
crug_pal_sleep_ns (uint64_t ns)
{
  atomic_fetch_add_explicit (&fake_clock_ns, ns, memory_order_relaxed);
}
