/* pal_contract.c - the backend contract suite of the Crug PAL.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Every case is written against <crug_pal.h> only.  The same source is linked
   with the fake backend and with the Linux backend, which is what makes the
   contract a contract instead of a description of one port (ADR 0011 D5).

   A case may be SKIPPED only when the header lets a backend decline the
   capability and the backend says so through the contract (guard pages).
   Any other skip is a failure of this suite.
*/

#include "pal_contract.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static unsigned case_index;
static unsigned n_ok;
static unsigned n_skip;
static unsigned n_fail;

static void
report (crug_pal_reporter reporter, void *ctx, const char *name,
        crug_pal_verdict verdict, const char *detail)
{
  if (reporter != NULL)
    reporter (ctx, name, verdict, detail != NULL ? detail : "");
}

static void
pass (crug_pal_reporter reporter, void *ctx, const char *name)
{
  case_index++;
  n_ok++;
  report (reporter, ctx, name, CRUG_PAL_CASE_OK, "");
}

static void
skip (crug_pal_reporter reporter, void *ctx, const char *name, const char *why)
{
  case_index++;
  n_skip++;
  report (reporter, ctx, name, CRUG_PAL_CASE_SKIP, why);
}

static void
fail (crug_pal_reporter reporter, void *ctx, const char *name, const char *why)
{
  case_index++;
  n_fail++;
  report (reporter, ctx, name, CRUG_PAL_CASE_FAIL, why);
}

/* ---------------------------------------------------------------- threads */

static _Atomic uint32_t thread_flag;

static void
thread_entry (void *arg)
{
  (void) arg;
  atomic_store_explicit (&thread_flag, 1, memory_order_release);
}

static void
case_thread_create_join (crug_pal_reporter reporter, void *ctx)
{
  crug_pal_thread *thread = NULL;
  atomic_store_explicit (&thread_flag, 0, memory_order_relaxed);
  if (crug_pal_thread_create (&thread, thread_entry, NULL) != 0)
    {
      fail (reporter, ctx, "thread-create-join", "create failed");
      return;
    }
  if (thread == NULL)
    {
      fail (reporter, ctx, "thread-create-join", "create returned no handle");
      return;
    }
  if (crug_pal_thread_join (thread) != 0)
    {
      fail (reporter, ctx, "thread-create-join", "join reported failure");
      return;
    }
  if (atomic_load_explicit (&thread_flag, memory_order_acquire) != 1)
    fail (reporter, ctx, "thread-create-join", "entry did not run before join returned");
  else
    pass (reporter, ctx, "thread-create-join");
}

/* A NULL handle is a contract violation, and the failure must be reported
   through the error slot: the core relies on join telling the truth so that a
   failed shutdown does not free a worker it never joined.  */
static void
case_thread_join_rejects_null (crug_pal_reporter reporter, void *ctx)
{
  crug_pal_clear_error ();
  if (crug_pal_thread_join (NULL) == 0)
    fail (reporter, ctx, "thread-join-rejects-null", "a NULL handle was joined");
  else if (crug_pal_last_error () != CRUG_PAL_EINVAL)
    fail (reporter, ctx, "thread-join-rejects-null", "the failure was not reported as EINVAL");
  else
    pass (reporter, ctx, "thread-join-rejects-null");
}

static void
case_thread_rejects_null_entry (crug_pal_reporter reporter, void *ctx)
{
  crug_pal_thread *thread = NULL;
  int rc;

  crug_pal_clear_error ();
  rc = crug_pal_thread_create (&thread, NULL, NULL);
  if (rc == 0)
    fail (reporter, ctx, "thread-rejects-null-entry", "a NULL entry was accepted");
  else if (crug_pal_last_error () != CRUG_PAL_EINVAL)
    fail (reporter, ctx, "thread-rejects-null-entry", "the failure was not reported as EINVAL");
  else
    pass (reporter, ctx, "thread-rejects-null-entry");
}

static void
case_cpu_count (crug_pal_reporter reporter, void *ctx)
{
  uint32_t count = crug_pal_cpu_count ();
  if (count < 1)
    fail (reporter, ctx, "cpu-count", "reported zero processors");
  else if (count > 65536u)
    fail (reporter, ctx, "cpu-count", "reported an implausible processor count");
  else
    pass (reporter, ctx, "cpu-count");
}

/* -------------------------------------------------------------- wait/wake */

static _Atomic uint32_t wake_flag;
static _Atomic uint32_t wake_seen;

static void
wake_entry (void *arg)
{
  (void) arg;
  while (atomic_load_explicit (&wake_flag, memory_order_acquire) == 0)
    crug_pal_wait_u32 (&wake_flag, 0);
  atomic_store_explicit (&wake_seen, 1, memory_order_release);
}

static void
case_wait_wake (crug_pal_reporter reporter, void *ctx)
{
  crug_pal_thread *thread = NULL;

  atomic_store_explicit (&wake_flag, 0, memory_order_relaxed);
  atomic_store_explicit (&wake_seen, 0, memory_order_relaxed);
  if (crug_pal_thread_create (&thread, wake_entry, NULL) != 0)
    {
      fail (reporter, ctx, "wait-wake", "create failed");
      return;
    }
  crug_pal_sleep_ns (20000000u);
  atomic_store_explicit (&wake_flag, 1, memory_order_release);
  crug_pal_wake_all (&wake_flag);
  crug_pal_thread_join (thread);
  if (atomic_load_explicit (&wake_seen, memory_order_acquire) != 1)
    fail (reporter, ctx, "wait-wake", "the waiter did not observe the wake");
  else
    pass (reporter, ctx, "wait-wake");
}

static void
case_wait_returns_when_changed (crug_pal_reporter reporter, void *ctx)
{
  static _Atomic uint32_t word;
  uint64_t before, after;

  atomic_store_explicit (&word, 5, memory_order_relaxed);
  before = crug_pal_clock_monotonic_ns ();
  crug_pal_wait_u32 (&word, 0);
  after = crug_pal_clock_monotonic_ns ();
  if (after < before)
    fail (reporter, ctx, "wait-returns-when-changed", "the clock went backwards");
  else if (after - before > 2000000000u)
    fail (reporter, ctx, "wait-returns-when-changed", "a satisfied predicate still blocked");
  else
    pass (reporter, ctx, "wait-returns-when-changed");
}

/* ---------------------------------------------------------------- memory */

static void
case_memory_reserve_commit_release (crug_pal_reporter reporter, void *ctx)
{
  const size_t page = crug_pal_page_size ();
  size_t size;
  unsigned char *ptr;

  /* The runtime places a task's guard page on a page boundary, so the page
     size is part of the contract, not an implementation detail.  */
  if (page < (size_t) 4096 || (page & (page - 1)) != 0)
    {
      fail (reporter, ctx, "memory-reserve-commit-release",
	    "the page size is not a power of two of at least 4096");
      return;
    }
  size = page * 256;
  ptr = (unsigned char *) crug_pal_memory_reserve (size);

  if (ptr == NULL)
    {
      fail (reporter, ctx, "memory-reserve-commit-release", "reserve returned NULL");
      return;
    }
  if (!crug_pal_memory_commit (ptr, size))
    {
      crug_pal_memory_release (ptr, size);
      fail (reporter, ctx, "memory-reserve-commit-release", "commit failed");
      return;
    }
  ptr[0] = 0x5a;
  ptr[size - 1] = 0xa5;
  if (ptr[0] != 0x5a || ptr[size - 1] != 0xa5)
    {
      crug_pal_memory_release (ptr, size);
      fail (reporter, ctx, "memory-reserve-commit-release", "committed memory did not keep its bytes");
      return;
    }
  crug_pal_memory_release (ptr, size);
  pass (reporter, ctx, "memory-reserve-commit-release");
}

static void
case_memory_guard (crug_pal_reporter reporter, void *ctx)
{
  const size_t page = (size_t) 1 << 16;
  const size_t size = page * 2;
  unsigned char *ptr = (unsigned char *) crug_pal_memory_reserve (size);

  if (ptr == NULL)
    {
      fail (reporter, ctx, "memory-guard", "reserve returned NULL");
      return;
    }
  if (!crug_pal_memory_commit (ptr, page))
    {
      crug_pal_memory_release (ptr, size);
      fail (reporter, ctx, "memory-guard", "commit failed");
      return;
    }
  if (!crug_pal_memory_protect_guard (ptr + page, page))
    {
      crug_pal_memory_release (ptr, size);
      skip (reporter, ctx, "memory-guard", "backend declines guard pages");
      return;
    }
  crug_pal_memory_release (ptr, size);
  pass (reporter, ctx, "memory-guard");
}

/* ------------------------------------------------------------------ time */

static void
case_clock_monotonic (crug_pal_reporter reporter, void *ctx)
{
  uint64_t first = crug_pal_clock_monotonic_ns ();
  uint64_t second = crug_pal_clock_monotonic_ns ();
  uint64_t after;

  /* Two back-to-back readings may land in the same clock tick on a backend
     whose tick is coarser than the call, so the contract is that the clock
     never decreases and that it advances across a sleep.  A clock that
     silently freezes for a millisecond is still a contract violation.  */
  if (second < first)
    {
      fail (reporter, ctx, "clock-monotonic", "the clock went backwards");
      return;
    }
  crug_pal_sleep_ns (1000000u);
  after = crug_pal_clock_monotonic_ns ();
  if (after <= second)
    fail (reporter, ctx, "clock-monotonic",
          "the clock did not advance across a millisecond");
  else
    pass (reporter, ctx, "clock-monotonic");
}

static void
case_sleep_duration (crug_pal_reporter reporter, void *ctx)
{
  const uint64_t want = 2000000u;
  uint64_t before = crug_pal_clock_monotonic_ns ();
  crug_pal_sleep_ns (want);
  uint64_t after = crug_pal_clock_monotonic_ns ();

  if (after < before)
    fail (reporter, ctx, "sleep-duration", "the clock went backwards");
  else if (after - before < want)
    fail (reporter, ctx, "sleep-duration", "the sleep returned early");
  else
    pass (reporter, ctx, "sleep-duration");
}

/* --------------------------------------------------------- errors, version */

static void
case_error_mapping (crug_pal_reporter reporter, void *ctx)
{
  void *ptr;

  crug_pal_clear_error ();
  ptr = crug_pal_memory_reserve (0);
  if (ptr != NULL)
    {
      fail (reporter, ctx, "error-mapping", "a zero-size reservation succeeded");
      return;
    }
  if (crug_pal_last_error () != CRUG_PAL_EINVAL)
    {
      fail (reporter, ctx, "error-mapping", "a zero-size reservation did not report EINVAL");
      return;
    }
  ptr = crug_pal_memory_reserve (4096);
  if (ptr == NULL || crug_pal_last_error () != CRUG_PAL_OK)
    {
      fail (reporter, ctx, "error-mapping", "a successful call did not clear the error slot");
      return;
    }
  crug_pal_memory_release (ptr, 4096);
  pass (reporter, ctx, "error-mapping");
}

static void
case_version_and_names (crug_pal_reporter reporter, void *ctx)
{
  if (CRUG_PAL_VERSION_MAJOR != 1 || CRUG_PAL_VERSION_MINOR != 2)
    {
      fail (reporter, ctx, "version-and-names", "the suite was built against another contract version");
      return;
    }
  if (CRUG_PAL_ABI_VERSION != ((1 << 16) | 2))
    {
      fail (reporter, ctx, "version-and-names", "the ABI version encoding changed");
      return;
    }
  if (crug_pal_error_name (CRUG_PAL_OK) == NULL
      || crug_pal_error_name (CRUG_PAL_EINVAL) == NULL
      || crug_pal_error_name (CRUG_PAL_ENOMEM) == NULL
      || crug_pal_error_name (CRUG_PAL_EAGAIN) == NULL
      || crug_pal_error_name (CRUG_PAL_ENOSYS) == NULL
      || crug_pal_error_name (CRUG_PAL_ESYS) == NULL
      || crug_pal_error_name ((crug_pal_error) 99) == NULL)
    {
      fail (reporter, ctx, "version-and-names", "an error class has no spelling");
      return;
    }
  pass (reporter, ctx, "version-and-names");
}

static void
case_shutdown_idempotent (crug_pal_reporter reporter, void *ctx)
{
  crug_pal_shutdown ();
  crug_pal_shutdown ();
  pass (reporter, ctx, "shutdown-idempotent");
}

/* --------------------------------------------------------------- the suite */

void
crug_pal_contract_run (crug_pal_reporter reporter, void *ctx,
                       struct crug_pal_contract_summary *summary)
{
  case_index = 0;
  n_ok = 0;
  n_skip = 0;
  n_fail = 0;
  case_version_and_names (reporter, ctx);
  case_thread_create_join (reporter, ctx);
  case_thread_join_rejects_null (reporter, ctx);
  case_thread_rejects_null_entry (reporter, ctx);
  case_cpu_count (reporter, ctx);
  case_wait_wake (reporter, ctx);
  case_wait_returns_when_changed (reporter, ctx);
  case_memory_reserve_commit_release (reporter, ctx);
  case_memory_guard (reporter, ctx);
  case_clock_monotonic (reporter, ctx);
  case_sleep_duration (reporter, ctx);
  case_error_mapping (reporter, ctx);
  case_shutdown_idempotent (reporter, ctx);

  if (summary != NULL)
    {
      summary->cases = case_index;
      summary->ok = n_ok;
      summary->skipped = n_skip;
      summary->failed = n_fail;
    }
}
