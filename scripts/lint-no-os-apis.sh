#!/bin/sh
# lint-no-os-apis.sh - fail if an operating-system primitive leaks into the core.
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
#
# Usage: scripts/lint-no-os-apis.sh [DIRECTORY]     (default libcrug/core)
#
# The check is textual on purpose: it bans the *names*, so a comment that tells
# the next reader to "just call it here" fails too.  The gate list is the one
# in EXIT-GATES phase 13; the second list is RFC-0001 section 35 plus the Linux
# primitives a port would reach for first.  Directory-relative paths are taken
# from the repository root, absolute paths are used as given.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
target=${1:-libcrug/core}
case $target in
  /*) dir=$target ;;
  *) dir=$ROOT/$target ;;
esac

if [ ! -d "$dir" ]; then
  echo "lint-no-os-apis: not a directory: $target" >&2
  exit 2
fi

gate='pthread_|epoll|futex|CreateThread|kqueue|WaitOnAddress'
extra='clone|io_uring|VirtualAlloc|mach_|mmap|mprotect|munmap|syscall|gettimeofday|clock_gettime|clock_nanosleep|sysconf|nanosleep|fork'

files=$(find "$dir" -type f | wc -l)
gate_hits=$(grep -REn "$gate" "$dir" 2>/dev/null | wc -l)
extra_hits=$(grep -REn "$extra" "$dir" 2>/dev/null | wc -l)

if [ "$gate_hits" -ne 0 ] || [ "$extra_hits" -ne 0 ]; then
  echo "lint-no-os-apis: operating-system primitives in $target" >&2
  grep -REn "$gate" "$dir" 2>/dev/null >&2 || true
  grep -REn "$extra" "$dir" 2>/dev/null >&2 || true
  echo "lint-no-os-apis: $gate_hits gate match(es), $extra_hits extended match(es)" >&2
  exit 1
fi

echo "lint-no-os-apis: $files file(s) under $target, 0 operating-system primitive references"
