/* pal_windows.c - the Windows backend of the Crug PAL, contract version 1.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Built with -DCRUG_PAL_WINDOWS.  This is the only place in libcrug that names
   a Windows primitive; the portability lint fails the build if any of these
   names appear under libcrug/core (ADR 0011 D1, ADR 0012 D2).

     threads    CreateThread and WaitForSingleObject
     wait/wake  WaitOnAddress and WakeByAddress*, resolved at run time because
                a MinGW import library may predate Windows 8; a spin with Sleep
                is the fallback when the calls are not present
     memory     VirtualAlloc, VirtualProtect and VirtualFree
     clock      QueryPerformanceCounter
     sleep      Sleep

   Reservation commits eagerly: Windows address space is cheap, and the guard
   page of a task stack is only enforceable on committed memory, so reserve
   returns ReadWrite memory and commit is a no-op that keeps the contract.
*/

#include "crug_pal.h"

#include <windows.h>

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

static _Thread_local crug_pal_error windows_error = CRUG_PAL_OK;

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
  return windows_error;
}

void
crug_pal_clear_error (void)
{
  windows_error = CRUG_PAL_OK;
}

void
crug_pal_shutdown (void)
{
  windows_error = CRUG_PAL_OK;
}

struct crug_pal_thread
{
  HANDLE handle;
  int joined;
};

struct crug_pal_thread_start
{
  void (*entry) (void *);
  void *arg;
};

static DWORD WINAPI
crug_pal_thread_main (LPVOID opaque)
{
  struct crug_pal_thread_start start = *(struct crug_pal_thread_start *) opaque;
  free (opaque);
  start.entry (start.arg);
  return 0;
}

int
crug_pal_thread_create (crug_pal_thread **out, void (*entry) (void *), void *arg)
{
  struct crug_pal_thread_start *start;
  crug_pal_thread *thread;

  if (out == NULL || entry == NULL)
    {
      windows_error = CRUG_PAL_EINVAL;
      return -1;
    }
  thread = (crug_pal_thread *) calloc (1, sizeof *thread);
  start = (struct crug_pal_thread_start *) malloc (sizeof *start);
  if (thread == NULL || start == NULL)
    {
      free (thread);
      free (start);
      windows_error = CRUG_PAL_ENOMEM;
      return -1;
    }
  start->entry = entry;
  start->arg = arg;
  thread->handle = CreateThread (NULL, 0, crug_pal_thread_main, start, 0, NULL);
  if (thread->handle == NULL)
    {
      DWORD code = GetLastError ();
      free (thread);
      free (start);
      windows_error = (code == ERROR_NOT_ENOUGH_MEMORY || code == ERROR_OUTOFMEMORY)
	? CRUG_PAL_ENOMEM : CRUG_PAL_ESYS;
      return -1;
    }
  *out = thread;
  windows_error = CRUG_PAL_OK;
  return 0;
}

int
crug_pal_thread_join (crug_pal_thread *thread)
{
  DWORD rc;

  if (thread == NULL)
    {
      windows_error = CRUG_PAL_EINVAL;
      return -1;
    }
  rc = WaitForSingleObject (thread->handle, INFINITE);
  if (rc != WAIT_OBJECT_0)
    {
      windows_error = CRUG_PAL_ESYS;
      return -1;
    }
  if (!CloseHandle (thread->handle))
    {
      windows_error = CRUG_PAL_ESYS;
      return -1;
    }
  thread->joined = 1;
  free (thread);
  windows_error = CRUG_PAL_OK;
  return 0;
}

uint32_t
crug_pal_cpu_count (void)
{
  SYSTEM_INFO info;
  GetSystemInfo (&info);
  if (info.dwNumberOfProcessors < 1)
    return 1;
  return (uint32_t) info.dwNumberOfProcessors;
}

/* ---- wait/wake ------------------------------------------------------- */

typedef BOOL (WINAPI *crug_wait_on_address_fn) (volatile void *, void *,
						SIZE_T, DWORD);
typedef void (WINAPI *crug_wake_by_address_fn) (volatile void *, BOOL, ULONG);

static crug_wait_on_address_fn wait_on_address;
static crug_wake_by_address_fn wake_by_address_one;
static crug_wake_by_address_fn wake_by_address_all;
static int wait_resolved;

static void
resolve_wait (void)
{
  HMODULE kernel;
  if (wait_resolved)
    return;
  wait_resolved = 1;
  kernel = GetModuleHandleA ("KernelBase.dll");
  if (kernel == NULL)
    kernel = LoadLibraryA ("KernelBase.dll");
  if (kernel == NULL)
    kernel = GetModuleHandleA ("API-MS-Win-Core-Synch-L1-2-0.dll");
  if (kernel == NULL)
    return;
  wait_on_address = (crug_wait_on_address_fn) (void *) GetProcAddress (kernel, "WaitOnAddress");
  wake_by_address_one = (crug_wake_by_address_fn) (void *) GetProcAddress (kernel, "WakeByAddressSingle");
  wake_by_address_all = (crug_wake_by_address_fn) (void *) GetProcAddress (kernel, "WakeByAddressAll");
}

void
crug_pal_wait_u32 (_Atomic uint32_t *addr, uint32_t expected)
{
  if (addr == NULL)
    return;
  resolve_wait ();
  while (atomic_load_explicit (addr, memory_order_acquire) == expected)
    {
      if (wait_on_address != NULL)
	{
	  wait_on_address ((volatile void *) addr, (void *) &expected,
			   sizeof expected, INFINITE);
	  continue;
	}
      /* No WaitOnAddress: make the caller's predicate authoritative, the same
	 way the Linux backend degrades on a kernel without futex.  */
      windows_error = CRUG_PAL_ENOSYS;
      crug_pal_sleep_ns (1000000);
    }
}

void
crug_pal_wake_one (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  resolve_wait ();
  if (wake_by_address_one != NULL)
    wake_by_address_one ((volatile void *) addr, FALSE, 1);
  else
    windows_error = CRUG_PAL_ENOSYS;
}

void
crug_pal_wake_all (_Atomic uint32_t *addr)
{
  if (addr == NULL)
    return;
  resolve_wait ();
  if (wake_by_address_all != NULL)
    wake_by_address_all ((volatile void *) addr, FALSE, 1);
  else
    windows_error = CRUG_PAL_ENOSYS;
}

/* ---- memory ---------------------------------------------------------- */

size_t
crug_pal_page_size (void)
{
  SYSTEM_INFO info;
  GetSystemInfo (&info);
  return (size_t) info.dwPageSize;
}

void *
crug_pal_memory_reserve (size_t size)
{
  void *ptr;

  if (size == 0)
    {
      windows_error = CRUG_PAL_EINVAL;
      return NULL;
    }
  ptr = VirtualAlloc (NULL, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
  if (ptr == NULL)
    {
      windows_error = CRUG_PAL_ENOMEM;
      return NULL;
    }
  windows_error = CRUG_PAL_OK;
  return ptr;
}

bool
crug_pal_memory_commit (void *ptr, size_t size)
{
  if (ptr == NULL || size == 0)
    {
      windows_error = CRUG_PAL_EINVAL;
      return false;
    }
  /* Already committed by reserve; committing again keeps the bytes and lets
     the contract read the range.  */
  if (VirtualAlloc (ptr, size, MEM_COMMIT, PAGE_READWRITE) == NULL)
    {
      windows_error = CRUG_PAL_ESYS;
      return false;
    }
  windows_error = CRUG_PAL_OK;
  return true;
}

bool
crug_pal_memory_protect_guard (void *ptr, size_t size)
{
  DWORD old;
  if (ptr == NULL || size == 0)
    {
      windows_error = CRUG_PAL_EINVAL;
      return false;
    }
  if (!VirtualProtect (ptr, size, PAGE_NOACCESS, &old))
    {
      windows_error = CRUG_PAL_ESYS;
      return false;
    }
  windows_error = CRUG_PAL_OK;
  return true;
}

void
crug_pal_memory_release (void *ptr, size_t size)
{
  (void) size;
  if (ptr == NULL)
    return;
  if (!VirtualFree (ptr, 0, MEM_RELEASE))
    windows_error = CRUG_PAL_ESYS;
}

/* ---- clock and sleep ------------------------------------------------- */

static LARGE_INTEGER clock_frequency;
static int clock_ready;

uint64_t
crug_pal_clock_monotonic_ns (void)
{
  LARGE_INTEGER now;
  if (!clock_ready)
    {
      if (!QueryPerformanceFrequency (&clock_frequency) || clock_frequency.QuadPart == 0)
	{
	  windows_error = CRUG_PAL_ESYS;
	  return 0;
	}
      clock_ready = 1;
    }
  if (!QueryPerformanceCounter (&now))
    {
      windows_error = CRUG_PAL_ESYS;
      return 0;
    }
  /* Converting a 10 MHz counter to nanoseconds multiplies by 10^9, which
     overflows a signed 64-bit integer after about 922 seconds.  Divide the
     whole seconds first and scale only the remainder; the result saturates at
     UINT64_MAX (about 584 years), a range no monotonic clock reader can
     distinguish from "still running".  The 128-bit form is exact for every
     counter whose quotient fits; the fallback splits the fraction.  */
#if defined(__SIZEOF_INT128__)
  {
    __int128 ns = ((__int128) now.QuadPart * 1000000000)
		  / (__int128) clock_frequency.QuadPart;
    if (ns < 0)
      return 0;
    if (ns > (__int128) UINT64_MAX)
      return UINT64_MAX;
    return (uint64_t) ns;
  }
#else
  {
    uint64_t freq = (uint64_t) clock_frequency.QuadPart;
    uint64_t ticks = now.QuadPart < 0 ? 0 : (uint64_t) now.QuadPart;
    uint64_t whole;
    uint64_t rest;

    if (freq == 0)
      return 0;
    whole = ticks / freq;
    rest = ticks % freq;
    if (whole > (UINT64_MAX - 999999999ull) / 1000000000ull)
      return UINT64_MAX;
    if (freq <= UINT64_MAX / 1000000000ull)
      return whole * 1000000000ull + (rest * 1000000000ull) / freq;
    return whole * 1000000000ull
	   + (uint64_t) ((long double) rest * 1e9L / (long double) freq);
  }
#endif
}

void
crug_pal_sleep_ns (uint64_t ns)
{
  DWORD ms;
  if (ns == 0)
    return;
  ms = (DWORD) ((ns + 999999u) / 1000000u);
  if (ms == 0)
    ms = 1;
  Sleep (ms);
}
