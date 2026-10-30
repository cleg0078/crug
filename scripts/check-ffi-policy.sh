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
    echo "ffi-policy: no crugc (set CRUGC or run make crugc)" >&2
    exit 2
  fi
fi

# Every FFI diagnostic code must fire from a fixture, so the registry and the
# compiler agree (docs/crug/DIAGNOSTICS.md, ADR 0007 D4).  Two inputs cannot
# live in the repository and are generated here: the size/entry cap (FFI-010)
# and a sidecar with an embedded NUL byte (FFI-012).
SUITE="$ROOT/gcc/testsuite/crug.dg/ffi/phase9"
OUT="$ROOT/build/ffi-policy"
rm -rf "$OUT"
mkdir -p "$OUT"

rm -rf "$ROOT/build/ffi-policy-impl"
mkdir -p "$ROOT/build/ffi-policy-impl"
"$CRUGC" -O2 -c "$SUITE/contracts_impl.c" -o "$OUT/contracts_impl.o" > "$OUT/impl.log" 2>&1 || {
  echo "ffi-policy: cannot compile the fixture library" >&2
  exit 2
}

F="-fcrug-ffi=$SUITE/contracts.crugffi"
MISSING=0

expect () {
  code=$1
  shift
  log="$OUT/$code.log"
  "$CRUGC" "$@" > "$log" 2>&1
  if grep -q "crug-$code" "$log"; then
    :
  else
    MISSING=$((MISSING + 1))
    echo "MISSING crug-$code"
    sed -n '1,5p' "$log"
  fi
}

expect FFI-001 -O2 "-fcrug-ffi=$SUITE/bad/no-version.crugffi" "$SUITE/bad/no-version.c"
expect FFI-002 -O2 "-fcrug-ffi=$SUITE/bad/type-void.crugffi" "$SUITE/bad/type-void.c"
expect FFI-003 -O2 "-fcrug-ffi=$SUITE/bad/unknown-clause.crugffi" "$SUITE/bad/unknown-clause.c"
expect FFI-004 -O2 "-fcrug-ffi=$SUITE/conflict-a.crugffi" "-fcrug-ffi=$SUITE/conflict-b.crugffi" "$SUITE/conflict.c"
expect FFI-005 -O2 "-fcrug-ffi=$SUITE/bad/bad-arg.crugffi" "$SUITE/bad/bad-arg.c"
expect FFI-006 -O2 "-fcrug-ffi=$SUITE/bad/unmatched.crugffi" "$SUITE/bad/unmatched.c"
expect FFI-007 -O2 "$F" "$SUITE/reject-consume.c" "$OUT/contracts_impl.o"
expect FFI-008 -O2 "$F" "$SUITE/reject-out-owned-busy.c" "$OUT/contracts_impl.o"
expect FFI-009 -O2 "$F" "$SUITE/reject-callback.c" "$OUT/contracts_impl.o"
expect FFI-011 -O2 "-fcrug-ffi=$SUITE/bad/bad-dtor.crugffi" "$SUITE/bad/bad-dtor.c"

# FFI-010 is the size limit; the fixture would be 1 MiB in the repository, so
# it is generated here.
BIG="$OUT/big.crugffi"
{
  echo "crugffi 1.0;"
  i=0
  while [ $i -lt 20000 ]; do
    echo "# padding padding padding padding padding padding padding padding"
    i=$((i + 1))
  done
} > "$BIG"
"$CRUGC" -O2 "-fcrug-ffi=$BIG" "$SUITE/conflict.c" > "$OUT/FFI-010.log" 2>&1
if grep -q 'crug-FFI-010' "$OUT/FFI-010.log"; then
  :
else
  MISSING=$((MISSING + 1))
  echo "MISSING crug-FFI-010"
  sed -n '1,5p' "$OUT/FFI-010.log"
fi

# FFI-012 is a NUL byte inside a metadata line; a file with one cannot be
# stored in the repository (a text patch would drop it), so it is generated
# here too.  The suite generates the same input.
NUL="$OUT/nul.crugffi"
printf 'crugffi 1.0;\nfn spaced_free consumes arg0 nullable;\0@@@\n' > "$NUL"
expect FFI-012 -O2 "-fcrug-ffi=$NUL" "$ROOT/gcc/testsuite/crug.dg/ffi/metadata/argref-space.c"

# FFI-013 is a sidecar that ends inside a declaration; the suite keeps two
# repository fixtures for it and the policy runs one of them.
expect FFI-013 -O2 "-fcrug-ffi=$ROOT/gcc/testsuite/crug.dg/ffi/metadata/eof-type.crugffi" "$ROOT/gcc/testsuite/crug.dg/ffi/metadata/eof-type.c"

if [ $MISSING -eq 0 ]; then
  echo "ffi-policy: FFI-001 FFI-002 FFI-003 FFI-004 FFI-005 FFI-006 FFI-007 FFI-008 FFI-009 FFI-010 FFI-011 FFI-012 FFI-013"
  exit 0
fi
echo "ffi-policy: $MISSING codes did not fire"
exit 1
