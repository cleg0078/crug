#!/usr/bin/env bash
# Assert that crugc emits no hidden ownership state.
#
# Phase 1 forbids constructors, TLS, refcounts and libcrug linkage. The test
# compiles and links a minimal program with crugc and checks that no symbol or
# dynamic dependency mentions the Crug runtime.
set -euo pipefail

cd "$(dirname "$0")/.."

cc=${CRUGC:-gcc/crugc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

printf 'int main(void){return 0;}\n' > "$work/hello.c"
"$cc" -O2 -c "$work/hello.c" -o "$work/hello.o"
"$cc" -O2 "$work/hello.c" -o "$work/hello"

undef=$(nm -u "$work/hello.o" | grep -c '__crug_' || true)
defined=$(readelf -s "$work/hello" | grep -c '__crug_' || true)
dynamic=$(readelf -d "$work/hello" | grep -c 'libcrug' || true)

echo "hidden-state: undefined-symbols=$undef defined-symbols=$defined dynamic-libcrug=$dynamic"

if [ "$undef" -ne 0 ] || [ "$defined" -ne 0 ] || [ "$dynamic" -ne 0 ]; then
  echo "hidden-state: crugc emitted hidden ownership state" >&2
  exit 1
fi
