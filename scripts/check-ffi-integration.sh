#!/bin/sh
# Phase 9 FFI check.
#
# usage: scripts/check-ffi-integration.sh [-v]
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CRUGC=${CRUGC:-}
if [ -z "$CRUGC" ]; then
  if command -v crugc >/dev/null 2>&1; then
    CRUGC=$(command -v crugc)
  elif [ -x "$ROOT/gcc/crugc" ]; then
    CRUGC="$ROOT/gcc/crugc"
  else
    echo "ffi-integration: no crugc (set CRUGC or run make crugc)" >&2
    exit 2
  fi
fi

# The four libraries the Phase 9 gate names.  No dev packages are installed for
# sqlite3, so the programs declare their own prototypes and link the runtime
# sonames directly.
LIBDIR=${LIBDIR:-/usr/lib/x86_64-linux-gnu}
LIBS="libc.so.6 libsqlite3.so.0 libcurl.so.4 libssl.so.3"

INT="$ROOT/gcc/testsuite/crug.dg/ffi/integration"
LIBCFFI="$ROOT/gcc/testsuite/crug.dg/ffi/libc.crugffi"
OUT="$ROOT/build/ffi-integration"

for l in $LIBS; do
  if [ ! -f "$LIBDIR/$l" ]; then
    echo "ffi-integration: missing $LIBDIR/$l" >&2
    exit 2
  fi
done

rm -rf "$OUT"
mkdir -p "$OUT"

hashes () {
  for l in $LIBS; do sha256sum "$LIBDIR/$l"; done
}

hashes > "$OUT/before.txt" || exit 2

FAILS=0
check () {
  name=$1
  shift
  if "$@" > "$OUT/$name.out" 2>&1 && [ "$(cat "$OUT/$name.out")" != "" ]; then
    printf 'PASS %s: %s\n' "$name" "$(head -1 "$OUT/$name.out")"
  else
    FAILS=$((FAILS + 1))
    echo "FAIL $name"
    sed -n '1,10p' "$OUT/$name.out"
  fi
}

run_program () {
  "$OUT/$1"
}

"$CRUGC" -O2 "-fcrug-ffi=$LIBCFFI" "$INT/libc.c" -o "$OUT/libc" > "$OUT/libc.build" 2>&1 || FAILS=$((FAILS + 1))
"$CRUGC" -O2 "-fcrug-ffi=$INT/sqlite.crugffi" "$INT/sqlite.c" -L"$LIBDIR" -l:libsqlite3.so.0 -o "$OUT/sqlite" > "$OUT/sqlite.build" 2>&1 || FAILS=$((FAILS + 1))
"$CRUGC" -O2 "-fcrug-ffi=$INT/curl.crugffi" "$INT/curl.c" -L"$LIBDIR" -l:libcurl.so.4 -o "$OUT/curl" > "$OUT/curl.build" 2>&1 || FAILS=$((FAILS + 1))
"$CRUGC" -O2 "-fcrug-ffi=$INT/openssl.crugffi" "$INT/openssl.c" -L"$LIBDIR" -l:libssl.so.3 -l:libcrypto.so.3 -o "$OUT/openssl" > "$OUT/openssl.build" 2>&1 || FAILS=$((FAILS + 1))

for p in libc sqlite curl openssl; do
  if [ -x "$OUT/$p" ]; then
    check "$p" run_program "$p"
  else
    FAILS=$((FAILS + 1))
    echo "FAIL $p: did not build"
    sed -n '1,10p' "$OUT/$p.build"
  fi
done

grep -q 'opens=1 closes=1' "$OUT/sqlite.out" || { FAILS=$((FAILS + 1)); echo "FAIL sqlite: handle not closed exactly once"; }
grep -q 'inits=1 cleanups=1' "$OUT/curl.out" || { FAILS=$((FAILS + 1)); echo "FAIL curl: handle not cleaned up exactly once"; }
grep -q 'created=1 freed=1' "$OUT/openssl.out" || { FAILS=$((FAILS + 1)); echo "FAIL openssl: context not freed exactly once"; }

if hashes > "$OUT/after.txt" && cmp -s "$OUT/before.txt" "$OUT/after.txt"; then
  UNCHANGED=1
else
  UNCHANGED=0
fi

echo "ffi-integration: 4 libraries $([ $UNCHANGED -eq 1 ] && echo unchanged || echo CHANGED), 4 programs $([ $FAILS -eq 0 ] && echo OK || echo FAILING)"
[ $FAILS -eq 0 ] && [ $UNCHANGED -eq 1 ] || exit 1
exit 0
