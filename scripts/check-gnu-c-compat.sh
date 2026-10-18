#!/usr/bin/env bash
# GNU C regression with the Crug feature flags on.
#
# Usage: scripts/check-gnu-c-compat.sh
#
# Phase 3 must not change the meaning of any C program it is given: the flags
# exist so later phases can add productions, and until then every accepted
# translation unit keeps its meaning (ADR 0018 D1).  This script compiles the
# GNU C corpus and the kernel corpus with the full flag set, plus the macro
# fixture, and fails on the first translation unit that the flags broke.
set -uo pipefail

cd "$(dirname "$0")/.."

cc=${CRUGC:-gcc/crugc}
corpus=corpus
kernel=gcc/testsuite/crug.dg/kernel
macro=gcc/testsuite/crug.dg/flags/macro-regression.c

[ -x "$cc" ] || { echo "gnu-c-compat: crugc not executable: $cc" >&2; exit 2; }
[ -d "$corpus" ] || { echo "gnu-c-compat: no corpus directory: $corpus" >&2; exit 2; }
[ -d "$kernel" ] || { echo "gnu-c-compat: no kernel corpus: $kernel" >&2; exit 2; }

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

files=0
fails=0

check () {
  local src=$1
  shift
  files=$((files + 1))
  if ! "$cc" "$@" -fsyntax-only "$src" > "$work/out" 2> "$work/err"; then
    fails=$((fails + 1))
    printf 'FAIL: %s (%s)\n' "$src" "$*"
    sed 's/^/    /' "$work/err" | head -4
  fi
}

while IFS= read -r src; do
  check "$src" -fcrug-modern
done < <(find "$corpus" -name '*.c' -type f | sort)

while IFS= read -r src; do
  check "$src" -fcrug-kernel -ffreestanding -nostdlib
done < <(find "$kernel" -name '*.c' -type f | sort)

check "$macro" -fcrug-modern
check "$macro" -fcrug-kernel

echo "GNU_C_COMPAT: $files files, $fails FAIL"
[ "$fails" -eq 0 ]
