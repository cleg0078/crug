#!/bin/sh
# Phase 9 FFI check.
#
# usage: scripts/check-ffi-conflict.sh [-v]
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CRUGC=${CRUGC:-}
if [ -z "$CRUGC" ]; then
  if command -v crugc >/dev/null 2>&1; then
    CRUGC=$(command -v crugc)
  elif [ -x "$ROOT/gcc/crugc" ]; then
    CRUGC="$ROOT/gcc/crugc"
  else
    echo "ffi-conflict: no crugc (set CRUGC or run make crugc)" >&2
    exit 2
  fi
fi

# Two explicit sidecars disagree about 'peek'.  The conflict must be reported
# once, with a stable code, and the text must be byte-identical between runs
# (ADR 0007 D4).
SUITE="$ROOT/gcc/testsuite/crug.dg/ffi/phase9"
OUT="$ROOT/build/ffi-conflict"
rm -rf "$OUT"
mkdir -p "$OUT"

"$CRUGC" -O2 "-fcrug-ffi=$SUITE/conflict-a.crugffi" "-fcrug-ffi=$SUITE/conflict-b.crugffi" \
  "$SUITE/conflict.c" -o "$OUT/conflict" > "$OUT/run1.txt" 2>&1
RC1=$?
"$CRUGC" -O2 "-fcrug-ffi=$SUITE/conflict-a.crugffi" "-fcrug-ffi=$SUITE/conflict-b.crugffi" \
  "$SUITE/conflict.c" -o "$OUT/conflict" > "$OUT/run2.txt" 2>&1
RC2=$?

OK=1
[ $RC1 -ne 0 ] || { echo "ffi-conflict: first run compiled" >&2; OK=0; }
[ $RC2 -ne 0 ] || { echo "ffi-conflict: second run compiled" >&2; OK=0; }
grep -q 'crug-FFI-004' "$OUT/run1.txt" || { echo "ffi-conflict: no crug-FFI-004" >&2; OK=0; }
cmp -s "$OUT/run1.txt" "$OUT/run2.txt" || { echo "ffi-conflict: stderr differs between runs" >&2; OK=0; }

if [ $OK -eq 1 ]; then
  echo "ffi-conflict: crug-FFI-004 stable across 2 runs"
  exit 0
fi
sed -n '1,10p' "$OUT/run1.txt" >&2
exit 1
