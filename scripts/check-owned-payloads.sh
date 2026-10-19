#!/bin/sh
# check-owned-payloads.sh - ownership of payload enums that carry an owning
# payload: the active variant is destroyed exactly once, an inactive variant is
# never destroyed, and the unsafe forms are rejected.
#
# Compiles programs from gcc/testsuite/crug.dg/safe-types/ and asserts:
#   1. adts-drop.c produces exactly one drop, for its Text variant, and no drop
#      for the Number variant that is inactive in the same function;
#   2. payload-enum-own-run.c destroys the owning value once and the Number and
#      Quit values never;
#   3. the payload of an owning variant must be moved in, adopted or freshly
#      allocated (crug-ADT-019);
#   4. an owning payload enum is not copyable (crug-UAM-003);
#   5. matching it requires match (move (x)) (crug-ADT-015);
#   6. an arm for an owning variant binds its payload, and '_' cannot cover an
#      unseen owning variant (crug-ADT-020);
#   7. it cannot cross a function boundary by value (crug-ADT-018).
#
# Usage: scripts/check-owned-payloads.sh
# Environment: CRUGC overrides the driver path.
# Exit: 0 every property holds; 1 one of them fails; 2 cannot run.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DIR=$ROOT/gcc/testsuite/crug.dg/safe-types
OUT=$ROOT/build/owned-payloads

if [ -n "${CRUGC:-}" ]; then
	:
elif command -v crugc >/dev/null 2>&1; then
	CRUGC=crugc
elif [ -x "$ROOT/gcc/crugc" ]; then
	CRUGC=$ROOT/gcc/crugc
else
	echo "owned-payloads: no crugc driver found (build it with make baseline)" >&2
	exit 2
fi

rm -rf "$OUT"
mkdir -p "$OUT"

FLAGS="-O2 -fcrug-adt -fcrug-ownership"
bad=0

# 1. The drop dump names the active variant once and never the inactive one.
if "$CRUGC" $FLAGS -fcrug-dump=drop "$DIR/adts-drop.c" -o "$OUT/adts-drop" \
	>"$OUT/adts-drop.log" 2>&1; then
	dump=$OUT/adts-drop.crug-drop
	if [ ! -f "$dump" ]; then
		printf 'FAIL adts-drop: no drop dump written\n'
		bad=$((bad + 1))
	else
		drops=$(grep -c 'via destroy_buf' "$dump" || true)
		if [ "$drops" -ne 1 ]; then
			printf 'FAIL adts-drop: %s drops for the active variant, expected 1\n' "$drops"
			sed -n '1,5p' "$dump"
			bad=$((bad + 1))
		fi
		if ! grep -q '\[Text\]' "$dump"; then
			printf 'FAIL adts-drop: the drop does not name the Text variant\n'
			bad=$((bad + 1))
		fi
		if grep -q 'inactive' "$dump"; then
			printf 'FAIL adts-drop: the inactive Number value reports a drop\n'
			grep 'inactive' "$dump"
			bad=$((bad + 1))
		fi
	fi
	if ! "$OUT/adts-drop" >"$OUT/adts-drop.out" 2>&1; then
		printf 'FAIL adts-drop: exits non-zero\n'
		bad=$((bad + 1))
	fi
else
	printf 'FAIL adts-drop: does not compile\n'
	head -3 "$OUT/adts-drop.log"
	bad=$((bad + 1))
fi

# 2. The generated code tests the tag before it calls the destructor, so an
#    inactive variant never reaches the call even when the active variant is
#    not known statically.  The host is x86-64 (see check-safe-bounds.sh).
if "$CRUGC" -O0 -S -fcrug-adt -fcrug-ownership "$DIR/adts-drop.c" \
	-o "$OUT/adts-drop.s" >>"$OUT/adts-drop.log" 2>&1; then
	guard=$(grep -B10 -E 'call[[:space:]]+destroy_buf' "$OUT/adts-drop.s" \
		| grep -cE 'test[lq]|cmp[lq]' || true)
	if [ "$guard" -lt 2 ]; then
		printf 'FAIL adts-drop: the destructor call is not guarded by the tag and a null test\n'
		bad=$((bad + 1))
	fi
else
	printf 'FAIL adts-drop: does not compile to assembly\n'
	bad=$((bad + 1))
fi

# 3. The runtime counter: one destruction for Text, none for Number or Quit.
if "$CRUGC" $FLAGS "$DIR/payload-enum-own-run.c" -o "$OUT/payload-enum-own-run" \
	>"$OUT/own-run.log" 2>&1; then
	if ! "$OUT/payload-enum-own-run" >"$OUT/own-run.out" 2>&1; then
		printf 'FAIL payload-enum-own-run: a value was destroyed the wrong number of times\n'
		bad=$((bad + 1))
	fi
else
	printf 'FAIL payload-enum-own-run: does not compile\n'
	head -3 "$OUT/own-run.log"
	bad=$((bad + 1))
fi

# 4.-8. Each unsafe form is rejected with its documented code.
reject () {
	# reject <source> <code>
	if "$CRUGC" -fsyntax-only $FLAGS "$DIR/$1.c" >"$OUT/$1.log" 2>&1; then
		printf 'FAIL %s: accepted\n' "$1"
		bad=$((bad + 1))
		return
	fi
	if ! grep -q "\[$2\]" "$OUT/$1.log"; then
		printf 'FAIL %s: rejected without %s\n' "$1" "$2"
		head -3 "$OUT/$1.log"
		bad=$((bad + 1))
	fi
}

reject payload-enum-own-bare crug-ADT-019
reject payload-enum-own-copy crug-UAM-003
reject payload-enum-own-scrutinee crug-ADT-015
reject payload-enum-own-unbound crug-ADT-020
reject payload-enum-own-wildcard crug-ADT-020
reject payload-enum-own-param crug-ADT-018

if [ "$bad" -ne 0 ]; then
	echo "owned-payloads: $bad properties failed"
	exit 1
fi

echo "owned-payloads: active variant dropped once, inactive never, six unsafe forms rejected"
exit 0
