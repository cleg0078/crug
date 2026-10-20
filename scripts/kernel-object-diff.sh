#!/bin/sh
# kernel-object-diff.sh - Phase 19: does crugc change the code GCC emits for the
# pinned kernel corpus?
#
# crugc is a compatibility launcher: for plain C it must add no option, no
# builtin default and no optimisation. The check compiles the pinned corpus of
# kernel/KERNEL-CORPUS.md twice from the same tree and configuration - once
# through CC=gcc/crugc (stage K5's object directory) and once through the GCC
# driver the launcher wraps - and compares every object byte for byte.
#
# A byte difference is not automatically a defect: it may be a deliberate,
# documented consequence of the launcher. Every difference must then have a line
# in kernel/object-diff-allow.txt:
#
#   <object path>  <one-line explanation>
#
# The phase-19 gate wants "every difference absent or explained", so an empty
# allow file is the expected state.
#
# Usage:
#   scripts/kernel-object-diff.sh [--ref DIR]
#
# Environment:
#   KSRC=dir   extracted pinned tree   (default build/linux-6.18.57)
#   KO=dir     crugc object directory  (default build/kernel-crug)
#   REF=dir    reference object dir    (default build/kernel-ref)
#   XGCC=drv   the wrapped GCC driver  (default build/obj/gcc/xgcc)
#   JOBS=n     parallelism             (default: online CPUs)
#
# Exit status: 0 every object identical or explained, 1 an unexplained
# difference or a missing object, 2 a prerequisite is missing.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=kernel-object-diff
CORPUS=$ROOT/kernel/KERNEL-CORPUS.md
ALLOW=$ROOT/kernel/object-diff-allow.txt
KERNEL_VERSION=6.18.57

KSRC=${KSRC:-$ROOT/build/linux-6.18.57}
KO=${KO:-$ROOT/build/kernel-crug}
REF=${REF:-$ROOT/build/kernel-ref}
XGCC=${XGCC:-$ROOT/build/obj/gcc/xgcc}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
STAGES=${STAGES:-$ROOT/build/kernel-stages}

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--ref) REF=${2:-}; [ -n "$REF" ] || die "--ref needs a directory"; shift ;;
	-h|--help) sed -n '2,37p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

case $XGCC in /*) ;; *) XGCC=$ROOT/$XGCC ;; esac
[ -x "$XGCC" ] || die "no GCC driver at $XGCC (build the baseline first: make baseline)"
# An uninstalled xgcc finds cc1 through -B, which the crugc launcher adds for
# its own branches; the reference build spells it out so both runs use the
# same compiler search path.
REF_CC="$XGCC -B$(dirname -- "$XGCC")/"
[ -d "$KSRC" ] || die "no kernel tree at $KSRC"
[ -f "$CORPUS" ] || die "missing $CORPUS"
[ -d "$KO" ] || die "no crugc object directory at $KO (run scripts/kernel-stage.sh K5)"
mkdir -p "$STAGES" || die "cannot create $STAGES"

if [ -d "$ROOT/build/deps/usr/include" ]; then
	CPATH=${CPATH:+$CPATH:}$ROOT/build/deps/usr/include
	export CPATH
	if [ -d "$ROOT/build/deps/lib" ]; then
		LIBRARY_PATH=${LIBRARY_PATH:+$LIBRARY_PATH:}$ROOT/build/deps/lib
		export LIBRARY_PATH
	fi
fi

corpus_list () {
	awk '/^<!-- corpus:begin -->[ \t]*$/ {on = 1; next}
	     /^<!-- corpus:end -->[ \t]*$/   {on = 0; next}
	     on && $0 ~ /^[A-Za-z0-9_.\/-]+\.o$/ {print}' "$CORPUS"
}

corpus_list >"$STAGES/object-diff-corpus.txt"
total=$(wc -l <"$STAGES/object-diff-corpus.txt" | tr -d ' ')
[ "$total" -gt 0 ] || die "$CORPUS has no corpus objects"

# The reference build must use the same configuration, so derive it from the
# same defconfig and compare the results before trusting any object.
ref_make () {
	make -C "$KSRC" O="$REF" -j"$JOBS" CC="$REF_CC" "$@"
}

if [ ! -f "$REF/.config" ] || ! cmp -s "$KO/.config" "$REF/.config"; then
	say "configuring the reference directory: x86_64_defconfig in $REF"
	mkdir -p "$REF" || die "cannot create $REF"
	ref_make x86_64_defconfig >"$STAGES/ref-defconfig.log" 2>&1 \
		|| { tail -n 20 "$STAGES/ref-defconfig.log" >&2
		     die "the reference defconfig failed"; }
fi
cmp -s "$KO/.config" "$REF/.config" \
	|| die "$KO and $REF do not share a configuration; rerun stage K5 after a config change"

say "building the reference: $total object(s) with $REF_CC"
while IFS= read -r o; do
	rm -f "$REF/$o"
done <"$STAGES/object-diff-corpus.txt"
if ! xargs -n 120 make -C "$KSRC" O="$REF" -j"$JOBS" CC="$REF_CC" \
		<"$STAGES/object-diff-corpus.txt" >"$STAGES/ref-build.log" 2>&1; then
	tail -n 40 "$STAGES/ref-build.log" >&2
	die "the reference build failed"
fi

same=0
explained=0
different=0
missing=0
while IFS= read -r o; do
	if [ ! -s "$KO/$o" ] || [ ! -s "$REF/$o" ]; then
		warn "missing object: $o"
		missing=$((missing + 1))
		continue
	fi
	if cmp -s "$KO/$o" "$REF/$o"; then
		same=$((same + 1))
		continue
	fi
	different=$((different + 1))
	reason=
	if [ -f "$ALLOW" ]; then
		reason=$(awk -v want="$o" '$1 == want {
			$1 = ""; sub(/^[ \t]+/, ""); print; exit }' "$ALLOW")
	fi
	if [ -n "$reason" ]; then
		say "explained: $o: $reason"
		explained=$((explained + 1))
	else
		warn "unexplained difference: $o"
	fi
done <"$STAGES/object-diff-corpus.txt"

[ "$missing" -eq 0 ] || die "$missing corpus object(s) missing (rerun stage K5 and K4)"
unexplained=$((different - explained))
if [ "$unexplained" -ne 0 ]; then
	warn "$unexplained unexplained object difference(s); record each in kernel/object-diff-allow.txt"
	exit 1
fi
say "$total object(s): $same identical, $explained explained, 0 unexplained"
exit 0
