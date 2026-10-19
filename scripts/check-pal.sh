#!/bin/sh
# check-pal.sh - the same contract suite on both backends, compared.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Builds and runs the contract suite against the fake backend and the Linux
# backend, then proves that the two runs cover the same cases and that the
# only difference is the capability one backend is allowed to decline
# (guard pages).  That comparison is what makes the suite a contract rather
# than a list of things one port happens to do (ADR 0024 D5).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
mkdir -p build

run_backend() {
  backend=$1
  out=build/pal-$backend.out
  if ! make -C libcrug check-PAL PAL=$backend > "$out" 2>&1; then
    echo "check-pal: the $backend backend does not pass the contract" >&2
    tail -25 "$out" >&2
    return 1
  fi
  log=libcrug/build/check-pal-$backend.log
  [ -f "$log" ] || { echo "check-pal: no log for $backend" >&2; return 1; }
  grep '^case ' "$log" | sed 's/^case \([^:]*\):.*$/\1/' | sort > build/pal-$backend.cases
  return 0
}

run_backend fake || exit 1
run_backend linux || exit 1

if ! diff -u build/pal-fake.cases build/pal-linux.cases > build/pal-cases.diff; then
  echo "check-pal: the backends do not cover the same cases" >&2
  cat build/pal-cases.diff >&2
  exit 1
fi

cases=$(wc -l < build/pal-fake.cases | tr -d ' ')
if [ "$cases" -lt 10 ]; then
  echo "check-pal: only $cases contract cases" >&2
  exit 1
fi

# Every case must pass on Linux; the fake may only decline guard pages.
linux_fail=$(grep -c 'FAIL' libcrug/build/check-pal-linux.log || true)
fake_fail=$(grep -c 'FAIL' libcrug/build/check-pal-fake.log || true)
fake_skips=$(grep '^case .*: skip' libcrug/build/check-pal-fake.log || true)
if [ "$linux_fail" -ne 0 ] || [ "$fake_fail" -ne 0 ]; then
  echo "check-pal: a case failed (linux $linux_fail, fake $fake_fail)" >&2
  exit 1
fi
if [ -n "$fake_skips" ] && [ "$fake_skips" != "case memory-guard: skip (backend declines guard pages)" ]; then
  echo "check-pal: the fake backend skipped something other than guard pages:" >&2
  echo "$fake_skips" >&2
  exit 1
fi
if grep -q ': skip (' libcrug/build/check-pal-linux.log; then
  echo "check-pal: the Linux backend must not skip a contract case" >&2
  exit 1
fi
if ! grep -q 'guard-fault: SIGSEGV on the guarded page as claimed' libcrug/build/check-pal-linux-guard.log; then
  echo "check-pal: the Linux guard page does not fault" >&2
  exit 1
fi

# The fake backend claims to touch no operating system at all: prove it by
# looking at the undefined symbols of its object.
fake_os=$(nm -u libcrug/build/fake/pal_fake.o 2>/dev/null \
  | grep -E 'pthread|futex|mmap|mprotect|munmap|syscall|clock_' | wc -l | tr -d ' ')
if [ "$fake_os" -ne 0 ]; then
  echo "check-pal: the fake backend references $fake_os operating-system symbol(s)" >&2
  nm -u libcrug/build/fake/pal_fake.o | grep -E 'pthread|futex|mmap|mprotect|munmap|syscall|clock_' >&2
  exit 1
fi

echo "check-pal: fake and linux share $cases contract cases, 0 failures; fake backend links no system primitive, guard pages fault on linux"
