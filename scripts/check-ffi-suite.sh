#!/bin/sh
# Phase 9: run the .crugffi contract fixtures (ADR 0007 D3, D11).
#
# Every case is compiled and, when it is a run test, executed.  A rejection
# case must fail to compile AND print its expected stable code.
#
# usage: scripts/check-ffi-suite.sh [-v]
# prints one line per case and a summary 'ffi-suite: N cases, M failures'.

set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SUITE="$ROOT/gcc/testsuite/crug.dg/ffi/phase9"
METADATA="$ROOT/gcc/testsuite/crug.dg/ffi/metadata"
OUT="$ROOT/build/ffi-suite"
VERBOSE=0
[ "${1:-}" = "-v" ] && VERBOSE=1

CRUGC=${CRUGC:-}
if [ -z "$CRUGC" ]; then
  # Prefer the compiler in this checkout: an installed crugc elsewhere on
  # PATH may be an older release and would hide the fixtures under test.
  if [ -x "$ROOT/gcc/crugc" ]; then
    CRUGC="$ROOT/gcc/crugc"
  elif command -v crugc >/dev/null 2>&1; then
    CRUGC=$(command -v crugc)
  else
    echo "ffi-suite: no crugc (set CRUGC or run make crugc)" >&2
    exit 2
  fi
fi

if [ ! -d "$SUITE" ]; then
  echo "ffi-suite: no fixture directory $SUITE" >&2
  exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

"$CRUGC" -O2 -c "$SUITE/contracts_impl.c" -o "$OUT/contracts_impl.o" >"$OUT/impl.log" 2>&1 || {
  echo "ffi-suite: cannot compile the fixture library" >&2
  sed -n '1,20p' "$OUT/impl.log" >&2
  exit 2
}

CASES=0
FAILS=0

case_run () {
  name=$1
  want_fail=$2
  code=$3
  src=$4
  shift 4
  flags=$*
  CASES=$((CASES + 1))
  log="$OUT/$name.log"
  "$CRUGC" -O2 $flags "$src" "$OUT/contracts_impl.o" -o "$OUT/$name" >"$log" 2>&1
  rc=$?
  ok=1
  why=""
  if [ "$want_fail" = 0 ]; then
    if [ $rc -ne 0 ]; then
      ok=0; why="compile failed"
    elif ! "$OUT/$name"; then
      ok=0; why="run failed"
    fi
  else
    if [ $rc -eq 0 ]; then
      ok=0; why="compiled but should be rejected"
    elif [ "$code" != - ] && ! grep -q "$code" "$log"; then
      ok=0; why="missing $code"
    fi
  fi
  if [ $ok -eq 1 ]; then
    echo "PASS $name"
  else
    FAILS=$((FAILS + 1))
    echo "FAIL $name: $why"
    [ $VERBOSE -eq 1 ] && sed -n '1,10p' "$log"
  fi
}

F="-fcrug-ffi=$SUITE/contracts.crugffi"

case_run returns-owned 0 - "$SUITE/run-returns-owned.c" $F
case_run consume-move 0 - "$SUITE/run-consume-move.c" $F
case_run consume-rejected 1 crug-FFI-007 "$SUITE/reject-consume.c" $F
case_run out-owned 0 - "$SUITE/run-out-owned.c" $F
case_run out-owned-busy 1 crug-FFI-008 "$SUITE/reject-out-owned-busy.c" $F
case_run borrows 0 - "$SUITE/run-borrows.c" $F -fcrug-borrow
case_run mut-borrows 0 - "$SUITE/run-mut-borrows.c" $F -fcrug-borrow
case_run shared-to-mut 1 crug-BORROW-016 "$SUITE/reject-shared-to-mut.c" $F -fcrug-borrow
case_run callback 0 - "$SUITE/run-callback.c" $F
case_run callback-rejected 1 crug-FFI-009 "$SUITE/reject-callback.c" $F
case_run nullable 0 - "$SUITE/run-nullable.c" $F
case_run adjacent 0 - "$SUITE/adjacent/use.c" -fcrug-ffi-discover

# Metadata-format cases (0.2.4).  Two sidecars would be noise in the tree and
# are generated here: a line carrying an embedded NUL byte (F-02) and one with
# 10,001 declarations (F-04).  The NUL case keeps a valid declaration before
# the NUL, so before the fix the file compiled cleanly.
printf 'crugffi 1.0;\nfn spaced_free consumes arg0 nullable;\0@@@\n' > "$OUT/nul-byte.crugffi"
{
  echo "crugffi 1.0;"
  i=0
  while [ $i -lt 10001 ]; do
    echo "type t;"
    i=$((i + 1))
  done
} > "$OUT/declcap.crugffi"

case_run argref-spaced 0 - "$METADATA/argref-space.c" "-fcrug-ffi=$METADATA/argref-space.crugffi"
case_run truncated-consumes 1 crug-FFI-003 "$METADATA/truncated-consumes.c" "-fcrug-ffi=$METADATA/truncated-consumes.crugffi"
case_run truncated-returns 1 crug-FFI-003 "$METADATA/truncated-returns.c" "-fcrug-ffi=$METADATA/truncated-returns.crugffi"
case_run returns-unowned 0 - "$METADATA/returns-unowned.c" "-fcrug-ffi=$METADATA/returns-unowned.crugffi"
case_run nul-byte 1 crug-FFI-012 "$METADATA/argref-space.c" "-fcrug-ffi=$OUT/nul-byte.crugffi"
case_run decl-cap 1 crug-FFI-010 "$METADATA/empty.c" "-fcrug-ffi=$OUT/declcap.crugffi"

# Sidecar conflicts: two explicit files disagree about one declaration and the
# run must fail with crug-FFI-004 (ADR 0007 D4).
CASES=$((CASES + 1))
"$CRUGC" -O2 "-fcrug-ffi=$SUITE/conflict-a.crugffi" "-fcrug-ffi=$SUITE/conflict-b.crugffi" \
  "$SUITE/conflict.c" -o "$OUT/conflict" >"$OUT/conflict.log" 2>&1
if [ $? -ne 0 ] && grep -q crug-FFI-004 "$OUT/conflict.log"; then
  echo "PASS conflict"
else
  FAILS=$((FAILS + 1))
  echo "FAIL conflict: expected rejection with crug-FFI-004"
fi

# Malformed sidecars.  Each driver includes ../contracts.h so that the only
# complaint is the sidecar itself.
for case in no-version:FFI-001 version-2:FFI-001 unknown-clause:FFI-003 bad-arg:FFI-005 bad-dtor:FFI-011 unmatched:FFI-006 \
	     dtor-arity:FFI-011 dtor-knr:FFI-011 dtor-variadic:FFI-011 \
	     argref-malformed:FFI-003 argref-range:FFI-005; do
  name=${case%%:*}
  code=${case##*:}
  CASES=$((CASES + 1))
  log="$OUT/bad-$name.log"
  "$CRUGC" -O2 "-fcrug-ffi=$SUITE/bad/$name.crugffi" "$SUITE/bad/$name.c" \
    -o "$OUT/bad-$name" >"$log" 2>&1
  if [ $? -ne 0 ] && grep -q "crug-$code" "$log"; then
    echo "PASS bad-$name"
  else
    FAILS=$((FAILS + 1))
    echo "FAIL bad-$name: expected rejection with crug-$code"
    [ $VERBOSE -eq 1 ] && sed -n '1,10p' "$log"
  fi
done

echo "ffi-suite: $CASES cases, $FAILS failures"
[ $FAILS -eq 0 ] || exit 1
exit 0
