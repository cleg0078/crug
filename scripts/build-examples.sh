#!/bin/sh
# build-examples.sh - build the Crug example programs with crugc.
#
# Every example in the directory is compiled with the flags its header names;
# the two rejected programs are compiled with -fsyntax-only to prove that they
# still fail with the documented diagnostic code.  The runtime examples (go,
# scope, chan) link the Linux runtime objects, which are built on demand.
#
# Usage: scripts/build-examples.sh [directory]
#        make examples                 (directory: examples/)
#        make examples EXAMPLES=<dir>  (any other directory)
# Env:   EXAMPLES_DIR=dir  source directory   (default examples/)
#        OUT=dir           output directory   (default build/examples)
#        CRUGC=path        compiler driver    (default gcc/crugc)
# Exit:  0 every program built and every present rejection confirmed; 1 a
#        program failed to build (or was accepted when it must be rejected);
#        2 the driver or the source directory is missing, or it holds no .c
#        file.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CRUGC=${CRUGC:-$ROOT/gcc/crugc}
OUT=${OUT:-$ROOT/build/examples}
EXAMPLES_DIR=${EXAMPLES_DIR:-${1:-examples}}
LIB=$ROOT/libcrug
FFI=$ROOT/gcc/testsuite/crug.dg/ffi/libc.crugffi
RUNTIME="$LIB/build/linux/task.o $LIB/build/linux/pal_linux.o $LIB/build/linux/switch.o $LIB/build/linux/stack.o $LIB/build/linux/chan.o"
built=0
rejected=0
runtime_built=0

case $EXAMPLES_DIR in
	/*) SRC=$EXAMPLES_DIR ;;
	*) SRC=$ROOT/$EXAMPLES_DIR ;;
esac
case $OUT in
	"$ROOT"/*) OUTDISP=${OUT#"$ROOT"/} ;;
	*) OUTDISP=$OUT ;;
esac

if [ ! -x "$CRUGC" ]; then
	echo "examples: $CRUGC is not executable; run 'make baseline' first" >&2
	exit 2
fi
if [ ! -d "$SRC" ]; then
	echo "examples: no such directory: $EXAMPLES_DIR" >&2
	exit 2
fi
set -- "$SRC"/*.c
if [ ! -e "$1" ]; then
	echo "examples: no .c file in $EXAMPLES_DIR" >&2
	exit 2
fi
mkdir -p "$OUT"

# The flags each known example was written for.  A file whose basename is not
# in the list is a plain C program and is compiled with no -fcrug-* flag, so
# EXAMPLES_DIR can point at a directory of your own programs.
lookup () {
	base=$1
	MNAME=${base%.c}
	MKIND=plain
	MFLAGS=
	MCODE=
	case $base in
	01-hello.c)
		MNAME=hello ;;
	02-auto-drop.c)
		MNAME=auto-drop; MFLAGS=-fcrug-ownership ;;
	03-move.c)
		MNAME=move; MFLAGS=-fcrug-ownership ;;
	04-use-after-move.c)
		MNAME=use-after-move; MFLAGS=-fcrug-ownership; MCODE=UAM-001 ;;
	05-borrow.c)
		MNAME=borrow; MFLAGS=-fcrug-borrow ;;
	06-borrow-conflict.c)
		MNAME=borrow-conflict; MFLAGS=-fcrug-borrow; MCODE=BORROW-002 ;;
	07-nll.c)
		MNAME=nll; MFLAGS=-fcrug-borrow ;;
	08-strict-mode.c)
		MNAME=strict-hybrid; MFLAGS=-fcrug-ownership ;;
	09-unsafe-asm.c)
		MNAME=unsafe-asm; MFLAGS=-fcrug-strict ;;
	10-ffi.c)
		MNAME=ffi; MFLAGS="-fcrug-ffi=$FFI" ;;
	11-safe-types.c)
		MNAME=safe-types; MFLAGS="-fcrug-slices -fcrug-result" ;;
	12-bounds.c)
		MNAME=bounds; MFLAGS=-fcrug-slices ;;
	13-payload-enum.c)
		MNAME=payload-enum; MFLAGS=-fcrug-adt ;;
	14-owned-payload.c)
		MNAME=owned-payload; MFLAGS="-fcrug-adt -fcrug-ownership" ;;
	15-alias-facts.c)
		MNAME=alias-facts; MFLAGS="-fcrug-borrow -fcrug-alias-facts" ;;
	16-goroutines.c)
		MNAME=ex-goroutines; MKIND=runtime; MFLAGS="-fcrug-goroutines -fcrug-ownership" ;;
	17-scope.c)
		MNAME=ex-scope; MKIND=runtime; MFLAGS="-fcrug-goroutines -fcrug-ownership -fcrug-borrow" ;;
	18-channels.c)
		MNAME=ex-channels; MKIND=runtime; MFLAGS="-fcrug-channels -fcrug-ownership" ;;
	19-send-sync.c)
		MNAME=ex-send-sync; MKIND=runtime
		MFLAGS="-fcrug-goroutines -fcrug-ownership -fcrug-borrow -fcrug-channels" ;;
	stress-test-mem.c)
		MNAME=stress; MFLAGS=-fcrug-borrow ;;
	esac
}

runtime_ready () {
	[ "$runtime_built" -eq 1 ] && return 0
	echo 'examples: building the libcrug runtime objects (PAL=linux)'
	if ! make -C "$LIB" PAL=linux >"$OUT/runtime.log" 2>&1; then
		echo "examples: make -C libcrug PAL=linux failed; see $OUT/runtime.log" >&2
		cat "$OUT/runtime.log" >&2
		exit 1
	fi
	for obj in $RUNTIME; do
		if [ ! -f "$obj" ]; then
			echo "examples: missing runtime object $obj" >&2
			exit 1
		fi
	done
	runtime_built=1
}

# build <binary> <source> <plain|runtime> [flag...]
build () {
	name=$1
	file=$2
	kind=$3
	shift 3
	if [ "$kind" = runtime ]; then
		runtime_ready
		if ! "$CRUGC" -O2 "$@" -I"$LIB/core" "$file" \
			$RUNTIME -pthread -o "$OUT/$name"; then
			echo "examples: FAILED to build $DISP" >&2
			exit 1
		fi
	else
		if ! "$CRUGC" -O2 "$@" "$file" -o "$OUT/$name"; then
			echo "examples: FAILED to build $DISP" >&2
			exit 1
		fi
	fi
	printf '  %s\n' "$OUTDISP/$name"
	built=$((built + 1))
}

# reject <binary> <source> <code> [flag...]: the program must not compile, and
# the diagnostic must carry [crug-<code>] (the spec makes a stable code part
# of the contract).
reject () {
	name=$1
	file=$2
	code=$3
	shift 3
	log=$OUT/$name.reject.log
	if "$CRUGC" -O2 "$@" -fsyntax-only "$file" >"$log" 2>&1; then
		echo "examples: $DISP compiled but must be rejected with [crug-$code]" >&2
		exit 1
	fi
	if ! grep -q "crug-$code" "$log"; then
		echo "examples: $DISP failed without [crug-$code]; see $log" >&2
		cat "$log" >&2
		exit 1
	fi
	printf '  rejected %-24s [crug-%s]\n' "$DISP" "$code"
	rejected=$((rejected + 1))
}

echo "examples: building $EXAMPLES_DIR/ into $OUTDISP/"
for file in "$@"; do
	DISP=$file
	case $file in
		"$ROOT"/*) DISP=${file#"$ROOT"/} ;;
	esac
	lookup "${file##*/}"
	if [ -n "$MCODE" ]; then
		reject "$MNAME" "$file" "$MCODE" $MFLAGS
	else
		build "$MNAME" "$file" "$MKIND" $MFLAGS
	fi
done

if [ "$rejected" -eq 0 ]; then
	printf 'examples: %s program(s) built in %s\n' "$built" "$OUTDISP"
else
	printf 'examples: %s program(s) built in %s, %s rejected as documented\n' \
		"$built" "$OUTDISP" "$rejected"
fi
