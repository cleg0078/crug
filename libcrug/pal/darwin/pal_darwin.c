/* pal_darwin.c - the Darwin (macOS) backend of the Crug PAL, contract version 1.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Built with -DCRUG_PAL_DARWIN.  This is the only place in libcrug that names
   a Darwin primitive; the portability lint fails the build if any of these
   names appear under libcrug/core (ADR 0011 D1, ADR 0012 D2).

     threads    POSIX threads
     wait/wake  a hashed table of mutex and condition variable pairs: Darwin
                has no futex and its ulock is private, so the PAL keeps the
                predicate authoritative and signals the bucket of the address
     memory     mmap, mprotect and munmap
     clock      clock_gettime (CLOCK_MONOTONIC)
     sleep      nanosleep

   The hashed wait is correct, not fast: a wake signals every waiter in the
   bucket and each of them rechecks its own predicate, so there is no lost
   wakeup and no dependence on address identity beyond the bucket index.
*/

#include "crug_pal.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define CRUG_PAL_BUCKETS 64u

struct crug_pal_bucket
{
  pthread_mutex_t lock;
  pthread_cond_t cond;
};

static struct crug_pal_bucket buckets[CRUG_PAL_BUCKETS] = {
  { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER },
  { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER },
  /* The rest are zero-initialised and initialised on first use, so the table
     does not need sixty-four literals.  */
};

static pthread_once_t buckets_once = PTHREAD_ONCE_INIT;

static void
init_buckets (void)
{
  unsigned i;
  for (i = 2; i < CRUG_PAL_BUCKETS; i++)
    {
      pthread_mutex_init (&buckets[i].lock, NULL);
      pthread_cond_init (&buckets[i].cond, NULL);
    }
}

static struct crug_pal_bucket *
bucket_of (const void *addr)
{
  uintptr_t value = (uintptr_t) addr;
  uintptr_t mixed = value ^ (value >> 7) ^ (value >> 13);
  pthread_once (&buckets_once, init_buckets);
  return &buckets[mixed % CRUG_PAL_BUCKETS];
}

static _Thread_local crug_pal_error darwin_error = CRUG_PAL_OK;

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
  return darwin_error;
}

void
crug_pal_clear_error (void)
{
  darwin_error = CRUG_PAL_OK;
}

void
crug_pal_shutdown (void)
{
  darwin_error = CRUG_PAL_OK;
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
      darwin_error = CRUG_PAL_EINVAL;
      return -1;
    }
  thread = (crug_pal_thread *) calloc (1, sizeof *thread);
  start = (struct crug_pal_thread_start *) malloc (sizeof *start);
  if (thread == NULL || start == NULL)
    {
      free (thread);
      free (start);
      darwin_error = CRUG_PAL_ENOMEM;
      return -1;
    }
  start->entry = entry;
  start->arg = arg;
  rc = pthread_create (&thread->handle, NULL, crug_pal_thread_main, start);
  if (rc != 0)
    {
      free (thread);
      free (start);
      darwin_error = (rc == ENOMEM || rc == EAGAIN) ? CRUG_PAL_ENOMEM : CRUG_PAL_ESYS;
      return -1;
    }
  *out = thread;
  darwin_error = CRUG_PAL_OK;
  return 0;
}

void
crug_pal_thread_join (crug_pal_thread *thread)
{
  if (thread == NULL)
    return;
  if (!thread->joined)
    {
      pthread_join (thread->handle, NULL);
      thread->joined = 1;
    }
  free (thread);
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
  struct crug_pal_bucket *bucket;

  if (addr == NULL)
    return;
  bucket = bucket_of (addr);
  pthread_mutex_lock (&bucket->lock);
  while (atomic_load_explicit (addr, memory_order_acquire) == expected)
    pthread_cond_wait (&bucket->cond, &bucket->lock);
  pthread_mutex_unlock (&bucket->lock);
}

void
crug_pal_wake_one (_Atomic uint32_t *addr)
{
  struct crug_pal_bucket *bucket;
  if (addr == NULL)
    return;
  bucket = bucket_of (addr);
  pthread_mutex_lock (&bucket->lock);
  pthread_cond_signal (&bucket->cond);
  pthread_mutex_unlock (&bucket->lock);
}

void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  struct crug_pal_bucket *bucket;
  if (addr == NULL)
    return;
  bucket = bucket_of (addr);
  pthread_mutex_lock (&bucket->lock);
  pthread_cond_broadcast (&bucket->cond);
  pthread_mutex_unlock (&bucket->lock);
}

void *
crug_pal_memory_reserve (size_t size)
{
  void *ptr;
  if (size == 0)
    {
      darwin_error = CRUG_PAL_EINVAL;
      return NULL;
    }
  ptr = mmap (NULL, size, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (ptr == MAP_FAILED)
    {
      darwin_error = CRUG_PAL_ENOMEM;
      return NULL;
    }
  darwin_error = CRUG_PAL_OK;
  return ptr;
}

bool
crug_pal_memory_commit (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      darwin_error = CRUG_PAL_EINVAL;
      return false;
    }
  if (mprotect (ptr, size, PROT_READ | PROT_WRITE) != 0)
    {
      darwin_error = CRUG_PAL_ESYS;
      return false;
    }
  darwin_error = CRUG_PAL_OK;
  return true;
}

bool
crug_pal_memory_protect_guard (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      darwin_error = CRUG_PAL_EINVAL;
      return false;
    }
  if (mprotect (ptr, size, PROT_NONE) != 0)
    {
      darwin_error = CRUG_PAL_ESYS;
      return false;
    }
  darwin_error = CRUG_PAL_OK;
  return true;
}

void
crug_pal_memory_release (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    return;
  if (munmap (ptr, size) != 0)
    darwin_error = CRUG_PAL_ESYS;
}

uint64_t
crug_pal_clock_monotonic_ns (void)
{
  struct timespec ts;
  if (clock_gettime (CLOCK_MONOTONIC, &ts) != 0)
    {
      darwin_error = CRUG_PAL_ESYS;
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
  while (nanosleep (&ts, &ts) == -1 && errno == EINTR)
    ;
}
