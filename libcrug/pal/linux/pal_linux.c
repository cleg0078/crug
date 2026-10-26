/* pal_linux.c - the Linux backend of the Crug PAL, contract version 1.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Built with -DCRUG_PAL_LINUX.  This is the only place in libcrug that names
   a Linux primitive; the portability lint (scripts/lint-no-os-apis.sh) fails
   the build if any of these names appear under libcrug/core (ADR 0011 D1).

     threads    POSIX threads (the spec's "pthread initially" choice)
     wait/wake  SYS_futex with the private futex word operations
     memory     mmap, mprotect, munmap
     clock      clock_gettime (CLOCK_MONOTONIC)
     sleep      clock_nanosleep (CLOCK_MONOTONIC)
*/

/* The build uses -std=c11, which hides the POSIX and Linux declarations
   unless the backend asks for them explicitly.  This is a backend-local
   concern: the header and the core stay in plain C11.  */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "crug_pal.h"

#include <errno.h>
#include <limits.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#ifndef SYS_futex
#error "this Linux target has no futex system call number; the PAL v1 backend needs one"
#endif

static _Thread_local crug_pal_error linux_error = CRUG_PAL_OK;

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
  return linux_error;
}

void
crug_pal_clear_error (void)
{
  linux_error = CRUG_PAL_OK;
}

void
crug_pal_shutdown (void)
{
  linux_error = CRUG_PAL_OK;
}

struct crug_pal_thread
{
  pthread_t handle;
  int joined;
};

struct crug_pal_thread_start
{
  void (*entry) (void *);
  void *arg;
};

static void *
crug_pal_thread_main (void *opaque)
{
  struct crug_pal_thread_start start = *(struct crug_pal_thread_start *) opaque;
  free (opaque);
  start.entry (start.arg);
  return NULL;
}

int
crug_pal_thread_create (crug_pal_thread **out, void (*entry) (void *), void *arg)
{
  struct crug_pal_thread_start *start;
  crug_pal_thread *thread;
  int rc;

  if (out == NULL || entry == NULL)
    {
      linux_error = CRUG_PAL_EINVAL;
      return -1;
    }
  thread = (crug_pal_thread *) calloc (1, sizeof *thread);
  start = (struct crug_pal_thread_start *) malloc (sizeof *start);
  if (thread == NULL || start == NULL)
    {
      free (thread);
      free (start);
      linux_error = CRUG_PAL_ENOMEM;
      return -1;
    }
  start->entry = entry;
  start->arg = arg;
  rc = pthread_create (&thread->handle, NULL, crug_pal_thread_main, start);
  if (rc != 0)
    {
      free (thread);
      free (start);
      linux_error = (rc == ENOMEM || rc == EAGAIN) ? CRUG_PAL_ENOMEM : CRUG_PAL_ESYS;
      return -1;
    }
  *out = thread;
  linux_error = CRUG_PAL_OK;
  return 0;
}

int
crug_pal_thread_join (crug_pal_thread *thread)
{
  int rc;

  if (thread == NULL)
    {
      linux_error = CRUG_PAL_EINVAL;
      return -1;
    }
  rc = pthread_join (thread->handle, NULL);
  if (rc != 0)
    {
      linux_error = CRUG_PAL_ESYS;
      return -1;
    }
  thread->joined = 1;
  free (thread);
  linux_error = CRUG_PAL_OK;
  return 0;
}

uint32_t
crug_pal_cpu_count (void)
{
  long count = sysconf (_SC_NPROCESSORS_ONLN);
  if (count < 1)
    return 1;
  if (count > (long) UINT32_MAX)
    return UINT32_MAX;
  return (uint32_t) count;
}

void
crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected)
{
  if (addr == NULL)
    return;
  /* Loop on the predicate: a futex wait may return spuriously, and the
     contract makes the predicate authoritative, never a wake token.  */
  while (atomic_load_explicit (addr, memory_order_acquire) == expected)
    {
      long rc = syscall (SYS_futex, (uint32_t *) addr, FUTEX_WAIT_PRIVATE,
                         expected, NULL, NULL, 0);
      if (rc == 0 || errno == EAGAIN || errno == EINTR)
        continue;
      if (errno == ENOSYS)
        {
          /* A kernel without the call cannot block; degrade to spinning so
             the caller still makes progress, and report the limitation.  */
          linux_error = CRUG_PAL_ENOSYS;
          while (atomic_load_explicit (addr, memory_order_acquire) == expected)
            crug_pal_sleep_ns (1000000);
          return;
        }
      linux_error = CRUG_PAL_ESYS;
      return;
    }
}

void
crug_pal_wake_one (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  if (syscall (SYS_futex, (uint32_t *) addr, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0) < 0)
    linux_error = CRUG_PAL_ESYS;
}

void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  if (syscall (SYS_futex, (uint32_t *) addr, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0) < 0)
    linux_error = CRUG_PAL_ESYS;
}

size_t
crug_pal_page_size (void)
{
  long page = sysconf (_SC_PAGESIZE);
  return page > 0 ? (size_t) page : (size_t) 4096;
}

void *
crug_pal_memory_reserve (size_t size)
{
  void *ptr;

  if (size == 0)
    {
      linux_error = CRUG_PAL_EINVAL;
      return NULL;
    }
  ptr = mmap (NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ptr == MAP_FAILED)
    {
      linux_error = CRUG_PAL_ENOMEM;
      return NULL;
    }
  linux_error = CRUG_PAL_OK;
  return ptr;
}

bool
crug_pal_memory_commit (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      linux_error = CRUG_PAL_EINVAL;
      return false;
    }
  if (mprotect (ptr, size, PROT_READ | PROT_WRITE) != 0)
    {
      linux_error = CRUG_PAL_ESYS;
      return false;
    }
  linux_error = CRUG_PAL_OK;
  return true;
}

bool
crug_pal_memory_protect_guard (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      linux_error = CRUG_PAL_EINVAL;
      return false;
    }
  if (mprotect (ptr, size, PROT_NONE) != 0)
    {
      linux_error = CRUG_PAL_ESYS;
      return false;
    }
  linux_error = CRUG_PAL_OK;
  return true;
}

void
crug_pal_memory_release (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    return;
  if (munmap (ptr, size) != 0)
    linux_error = CRUG_PAL_ESYS;
}

uint64_t
crug_pal_clock_monotonic_ns (void)
{
  struct timespec ts;
  if (clock_gettime (CLOCK_MONOTONIC, &ts) != 0)
    {
      linux_error = CRUG_PAL_ESYS;
      return 0;
    }
  return (uint64_t) ts.tv_sec * 1000000000u + (uint64_t) ts.tv_nsec;
}

void
crug_pal_sleep_ns (uint64_t ns)
{
  struct timespec ts;
  ts.tv_sec = (time_t) (ns / 1000000000u);
  ts.tv_nsec = (long) (ns % 1000000000u);
  while (clock_nanosleep (CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR)
    ;
}
