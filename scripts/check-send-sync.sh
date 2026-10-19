#!/bin/sh
# check-send-sync.sh - the Phase 17 Send/Sync checks.
#
# Runs the Send/Sync DejaGnu suite, then shows that legal captures still
# build and run against the task runtime, and finally exercises the two
# escapes the rules accept at their edges: the FFI thread-affinity clause
# and the FFI send/sync type annotation.  The negative side of the rules
# (a raw pointer, a pointer-holding struct, a non-Sync shared borrow, an
# unmanaged channel element) is covered by the compiler suite.
#
# Usage: scripts/check-send-sync.sh
# Exit: 0 every check passes; 1 a check fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CC=$ROOT/gcc/crugc
BUILD=$ROOT/build/send-sync
LIB=$ROOT/libcrug
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o"
SUM=$ROOT/build/obj/gcc/testsuite/gcc/gcc.sum
FIX=$ROOT/gcc/testsuite/crug.dg/send-sync
fail=0

mkdir -p "$BUILD"

fail_msg () {
	echo "send-sync: $1" >&2
	fail=1
}

# 1. The runtime objects the runtime program links against.
if ! make -C "$LIB" PAL=linux >"$BUILD/runtime.log" 2>&1; then
	echo "send-sync: make -C libcrug PAL=linux failed; see $BUILD/runtime.log" >&2
	cat "$BUILD/runtime.log" >&2
	exit 1
fi
for obj in $RUNTIME; do
	if [ ! -f "$obj" ]; then
		echo "send-sync: missing runtime object $obj" >&2
		exit 1
	fi
done

# 2. The compiler suite: every case is compile-only with its own flags.
if ! make -C "$ROOT" check-SEND_SYNC_DG >"$BUILD/dg.log" 2>&1; then
	fail_msg "make check-SEND_SYNC_DG failed; see $BUILD/dg.log"
fi

dg_pass=$(grep -c '^PASS: crug.dg/send-sync/' "$SUM" 2>/dev/null || true)
dg_fail=$(grep -cE '^(FAIL|XPASS|UNRESOLVED|ERROR): crug.dg/send-sync/' "$SUM" 2>/dev/null || true)
if [ "$dg_pass" -eq 0 ]; then
	fail_msg "no crug.dg/send-sync tests ran"
fi
if [ "$dg_fail" -ne 0 ]; then
	fail_msg "$dg_fail crug.dg/send-sync test(s) failed"
fi

# 3. A permitted capture still runs: the Phase 14 spawn program is unchanged
#    and now passes the Send checks as well.
if ! $CC -O2 -fcrug-goroutines -I"$LIB/core" "$LIB/tests/goroutines/spawn.c" \
	-o "$BUILD/spawn" $RUNTIME -pthread >"$BUILD/spawn.log" 2>&1; then
	fail_msg "cannot build the spawn program; see $BUILD/spawn.log"
else
	if ! timeout 120 "$BUILD/spawn" >>"$BUILD/spawn.log" 2>&1; then
		fail_msg "the spawn program failed"
	else
		grep '^goroutines-spawn' "$BUILD/spawn.log" | tail -n 1
	fi
fi

# 4. The raw-pointer capture is rejected with the documented code (ADR 0026
#    D2).  This is the rule the gate quotes.
if $CC -fsyntax-only -fcrug-goroutines "$FIX/raw-ptr.c" >"$BUILD/raw.log" 2>&1; then
	fail_msg "a raw pointer capture was accepted"
elif ! grep -q 'crug-CONC-024' "$BUILD/raw.log"; then
	fail_msg "a raw pointer capture was not diagnosed with crug-CONC-024"
fi

# 5. A thread-affine value from an FFI function may not cross a task
#    boundary; the same call without the clause is accepted.
cat >"$BUILD/affine.c" <<'EOF'
int render_slot (void);

static int sink;

static void
use (int v)
{
  sink += v;
}

int
main (void)
{
  int s = render_slot ();
  go use (s);
  return 0;
}
EOF
printf 'crugffi 1.0;\nfn render_slot returns int thread_affinity render;\n' >"$BUILD/affine-affine.crugffi"
printf 'crugffi 1.0;\nfn render_slot returns int;\n' >"$BUILD/affine-plain.crugffi"
if $CC -fsyntax-only -fcrug-goroutines -fcrug-ffi="$BUILD/affine-affine.crugffi" "$BUILD/affine.c" >"$BUILD/affine.log" 2>&1; then
	fail_msg "a thread-affine value crossed a task boundary"
elif ! grep -q 'crug-CONC-025' "$BUILD/affine.log"; then
	fail_msg "a thread-affine capture was not diagnosed with crug-CONC-025"
fi
if ! $CC -fsyntax-only -fcrug-goroutines -fcrug-ffi="$BUILD/affine-plain.crugffi" "$BUILD/affine.c" >"$BUILD/affine-plain.log" 2>&1; then
	fail_msg "the same capture without the affinity clause was rejected"
	cat "$BUILD/affine-plain.log" >&2
fi

# 6. An FFI type annotation is the documented escape for an opaque type:
#    !send rejects the capture, send accepts it.  The same struct without
#    any annotation is rejected (fixture struct-ptr.c).
cat >"$BUILD/widget.c" <<'EOF'
struct Widget
{
  int *p;
};

static void
use (struct Widget w)
{
  (void) w;
}

int
main (void)
{
  struct Widget w;
  go use (w);
  return 0;
}
EOF
printf 'crugffi 1.0;\ntype struct Widget send sync;\n' >"$BUILD/widget-send.crugffi"
printf 'crugffi 1.0;\ntype struct Widget !send;\n' >"$BUILD/widget-notsend.crugffi"
if ! $CC -fsyntax-only -fcrug-goroutines -fcrug-ffi="$BUILD/widget-send.crugffi" "$BUILD/widget.c" >"$BUILD/widget-send.log" 2>&1; then
	fail_msg "the FFI 'send' annotation did not permit the capture"
	cat "$BUILD/widget-send.log" >&2
fi
if $CC -fsyntax-only -fcrug-goroutines -fcrug-ffi="$BUILD/widget-notsend.crugffi" "$BUILD/widget.c" >"$BUILD/widget-notsend.log" 2>&1; then
	fail_msg "the FFI '!send' annotation did not reject the capture"
elif ! grep -q 'crug-CONC-024' "$BUILD/widget-notsend.log"; then
	fail_msg "the FFI '!send' rejection did not carry crug-CONC-024"
fi

echo "send-sync: $dg_pass compiler test(s), $fail failure(s); raw pointer rejected, thread affinity enforced, FFI annotations enforced"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
exit 0
