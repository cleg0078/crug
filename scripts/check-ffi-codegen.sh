#!/bin/sh
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CRUGC=${CRUGC:-}
if [ -z "$CRUGC" ]; then
  if command -v crugc >/dev/null 2>&1; then
    CRUGC=$(command -v crugc)
  elif [ -x "$ROOT/gcc/crugc" ]; then
    CRUGC="$ROOT/gcc/crugc"
  else
    echo "ffi-codegen: no crugc (set CRUGC or run make crugc)" >&2
    exit 2
  fi
fi

# Metadata must never change the generated code (ADR 0007 D7): compiling the
# same plain C with and without a sidecar must produce byte-identical objects.
CORPUS="$ROOT/gcc/testsuite/crug.dg/ffi/phase9/clean"
LIBCFFI="$ROOT/gcc/testsuite/crug.dg/ffi/libc.crugffi"
OUT="$ROOT/build/ffi-codegen"

rm -rf "$OUT"
mkdir -p "$OUT/plain" "$OUT/ffi"

N=0
DIFF=0
for src in "$CORPUS"/*.c; do
  base=$(basename "$src" .c)
  N=$((N + 1))
  "$CRUGC" -O2 -fcrug-ownership -c "$src" -o "$OUT/plain/$base.o" > "$OUT/plain/$base.log" 2>&1 || DIFF=$((DIFF + 1))
  "$CRUGC" -O2 "-fcrug-ffi=$LIBCFFI" -c "$src" -o "$OUT/ffi/$base.o" > "$OUT/ffi/$base.log" 2>&1 || DIFF=$((DIFF + 1))
  if [ -f "$OUT/plain/$base.o" ] && [ -f "$OUT/ffi/$base.o" ]; then
    cmp -s "$OUT/plain/$base.o" "$OUT/ffi/$base.o" || { DIFF=$((DIFF + 1)); echo "DIFF $base"; }
  fi
done

echo "ffi-codegen: $N files, $DIFF differences"
[ $N -gt 0 ] || exit 2
[ $DIFF -eq 0 ] || exit 1
exit 0
