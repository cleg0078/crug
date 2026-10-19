/* crug_pal.h - the Crug Platform Abstraction Layer, contract version 1.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   This header is the ONLY way libcrug/core reaches an operating system.
   Backends live under libcrug/pal/<backend>/; the core never names a system
   call, a system thread primitive or a system wait primitive (ADR 0011).

   Status and error model
   ----------------------
   Every fallible call returns an int (0 on success, non-zero on failure).
   crug_pal_last_error() explains the most recent failure on the calling
   thread; it is CRUG_PAL_OK after a success.  crug_pal_error_name() maps a
   value to a stable spelling for diagnostics.  A backend that cannot provide
   an operation reports CRUG_PAL_ENOSYS; a capability that the contract lets a
   backend decline (guard pages) returns false instead of failing a call.

   Threading model
   ---------------
   crug_pal_thread_create() starts one entry function on one operating system
   thread.  The core does NOT build its task scheduler on this call in Phase
   13; the runtime multiplexes tasks over a configured number of workers from
   Phase 14 on.  crug_pal_thread_join() waits for completion and releases the
   handle; joining a handle twice is undefined.

   Wait/wake
   ---------
   crug_pal_wait_u32() blocks while *addr equals expected and returns when it
   does not (spurious returns are permitted, callers loop).  A wake with no
   waiter is not remembered: the wait predicate is the state, not a counter.
   Both calls require a naturally aligned _Atomic uint32_t and a single
   process (no shared-memory wake across processes in v1).

   Memory
   ------
   reserve() returns an inaccessible reservation of at least size bytes, or
   NULL.  commit() makes a sub-range readable and writable.  protect_guard()
   makes a sub-range inaccessible again; returning false means the backend has
   no guard-page support and the caller must not assume protection.
   release() returns a whole reservation obtained from reserve(); size must be
   the size passed to reserve().

   Time
   ----
   crug_pal_clock_monotonic_ns() is monotonic, never decreases, does not
   include suspend time where the backend can distinguish it, and has
   nanosecond resolution.  crug_pal_sleep_ns() is a request; the backend may
   over-sleep and must not under-sleep by more than one clock tick.
*/

#ifndef CRUG_PAL_H
#define CRUG_PAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRUG_PAL_VERSION_MAJOR 1
#define CRUG_PAL_VERSION_MINOR 0
#define CRUG_PAL_ABI_VERSION ((CRUG_PAL_VERSION_MAJOR << 16) | CRUG_PAL_VERSION_MINOR)

/* Stable error classes.  Values are part of the v1 contract: they may be
   compared for equality and appended to, never renumbered.  */
typedef enum crug_pal_error
{
  CRUG_PAL_OK = 0,
  CRUG_PAL_EINVAL = 1,   /* a contract precondition is violated          */
  CRUG_PAL_ENOMEM = 2,   /* the backend cannot obtain a resource         */
  CRUG_PAL_EAGAIN = 3,   /* retryable; the caller loops on its predicate */
  CRUG_PAL_ENOSYS = 4,   /* the backend does not implement the operation */
  CRUG_PAL_ESYS = 5      /* the operating system reported a failure      */
} crug_pal_error;

/* The spelling of an error class ("ok", "invalid", ...); never NULL.  */
const char *crug_pal_error_name (crug_pal_error error);

/* The error of the calling thread's most recent failed call.  */
crug_pal_error crug_pal_last_error (void);

/* Reset the calling thread's error slot to CRUG_PAL_OK.  */
void crug_pal_clear_error (void);

/* Release backend-global state.  Idempotent; the core calls it once at the end
   of a run and does not use the backend afterwards.  */
void crug_pal_shutdown (void);

/* Threads.  */
typedef struct crug_pal_thread crug_pal_thread;

int crug_pal_thread_create (crug_pal_thread **out,
                            void (*entry) (void *),
                            void *arg);
void crug_pal_thread_join (crug_pal_thread *thread);

/* The number of usable processors, at least 1.  */
uint32_t crug_pal_cpu_count (void);

/* Wait/wake on a 32-bit word.  */
void crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected);
void crug_pal_wake_one (_Atomic uint32_t *addr);
void crug_pal_wake_all (_Atomic uint32_t *addr);

/* Memory.  */
void *crug_pal_memory_reserve (size_t size);
bool crug_pal_memory_commit (void *ptr, size_t size);
bool crug_pal_memory_protect_guard (void *ptr, size_t size);
void crug_pal_memory_release (void *ptr, size_t size);

/* Time.  */
uint64_t crug_pal_clock_monotonic_ns (void);
void crug_pal_sleep_ns (uint64_t ns);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_PAL_H */
