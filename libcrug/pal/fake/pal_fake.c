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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CRUG_PAL_FAKE
#define CRUG_PAL_FAKE_BUILD 1
#endif

static _Thread_local crug_pal_error fake_error = CRUG_PAL_OK;

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

void
crug_pal_thread_join (crug_pal_thread *thread)
{
  if (thread == NULL)
    return;
  if (!thread->ran)
    {
      thread->ran = 1;
      thread->entry (thread->arg);
    }
  free (thread);
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

static uint64_t fake_clock_ns;

uint64_t
crug_pal_clock_monotonic_ns (void)
{
  /* Monotonic by construction: the fake clock advances one microsecond per
     reading, never depends on wall time, and a sleep advances it by exactly
     the requested amount so duration checks have a defined answer.  */
  fake_clock_ns += 1000;
  return fake_clock_ns;
}

void
crug_pal_sleep_ns (uint64_t ns)
{
  fake_clock_ns += ns;
}
