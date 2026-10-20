#!/bin/sh
# check-kernel-perf.sh - Phase 21: does the Crug kernel build generate the same
# machine code as the reference GCC build?
#
# scripts/kernel-object-diff.sh answers "is the object file the same?". This
# check answers the performance question behind it: for every object of the
# pinned corpus, compare the properties a codegen regression would move -
#
#   * byte identity       cmp(1), and the object file size
#   * generated code      .text size via size(1), disassembly digest via objdump -d
#   * symbol surface      nm(1) total, defined and undefined counts
#   * stack usage         .stack_sizes when the object carries it (readelf -x)
#
# Objects whose bytes differ are allowed only when kernel/object-diff-allow.txt
# explains them (same file and same format as kernel-object-diff.sh):
#   <object path>  <one-line explanation>
# An explained object is reported as "explained"; any other difference is
# "differing" and fails the check.
#
# The corpus is the <!-- corpus:begin --> block of kernel/KERNEL-CORPUS.md,
# paths relative to the object directory; both directories are build trees of
# the same pinned source, configuration and flags.
#
# Usage:
#   scripts/check-kernel-perf.sh [--jobs N] [--ko DIR] [--ref DIR]
#
# Environment:
#   KO=dir     crugc object directory   (default build/kernel-crug)
#   REF=dir    reference object dir     (default build/kernel-ref)
#   JOBS=n     parallelism              (default: online CPUs)
#   WORK=dir   scratch directory        (default build/kernel-perf)
#
# The last line printed has the shape
#   kernel-perf: 438 object(s): 437 identical, 1 explained, 0 differing (bytes N, text N, symbols N)
# where the aggregate numbers are totals over the Crug build. When the full
# build artefacts exist their sizes are recorded on the line before it.
#
# Exit status: 0 every object identical or explained, 1 an unexplained (or
# internally inconsistent) difference, 2 a prerequisite is missing.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
NAME=kernel-perf
CORPUS=$ROOT/kernel/KERNEL-CORPUS.md
ALLOW=$ROOT/kernel/object-diff-allow.txt

KO=${KO:-$ROOT/build/kernel-crug}
REF=${REF:-$ROOT/build/kernel-ref}
WORK=${WORK:-$ROOT/build/kernel-perf}
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}
ONE=

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

usage () { awk 'NR > 1 && /^set -u$/ { exit } NR > 1 { print }' "$0"; }

hash_cmd () {
	if command -v sha256sum >/dev/null 2>&1; then
		printf 'sha256sum\n'
	elif command -v shasum >/dev/null 2>&1; then
		printf 'shasum -a 256\n'
	elif command -v md5sum >/dev/null 2>&1; then
		printf 'md5sum\n'
	fi
}

# digest: the first field of the hash of stdin; $HASH may be two words.
digest () { $HASH | awk '{print $1}'; }

# allow_reason OBJ: the explanation recorded for OBJ, empty when there is none.
allow_reason () {
	[ -f "$ALLOW" ] || return 0
	awk -v want="$1" '$1 == want {
		$1 = ""; sub(/^[ \t]+/, ""); print; exit }' "$ALLOW"
}

# nm_counts OBJ: "total defined undefined" symbols of one object.
nm_counts () {
	nm "$1" 2>/dev/null | awk 'NF >= 3 { d++ } NF == 2 { u++ }
		END { print d + u, d + 0, u + 0 }'
}

# one_object OBJ: analyse one object pair and print a fixed-width record.
#
#   obj status bytes-crug bytes-ref text-crug text-ref sym-crug sym-ref
#       stack-crug stack-ref disassembly
#
# sym-crug and sym-ref are each "total defined undefined". status is "same"
# (byte identical), "diff" (bytes differ) or "missing"; the stack fields are
# "none" when the object carries no .stack_sizes section.
# The disassembly digest drops objdump's "<path>: file format ..." header line
# (some binutils print a blank line before it), so the same code read from two
# directories hashes the same.
one_object () {
	o=$1
	a=$KO/$o
	b=$REF/$o
	if [ ! -f "$a" ] || [ ! -f "$b" ]; then
		printf '%s missing - - - - - - - - - - - - -\n' "$o"
		return 0
	fi
	bA=$(wc -c <"$a" | tr -d ' ')
	bB=$(wc -c <"$b" | tr -d ' ')
	tA=$(size "$a" 2>/dev/null | awk 'NR == 2 { t = $1 } END { print t + 0 }')
	tB=$(size "$b" 2>/dev/null | awk 'NR == 2 { t = $1 } END { print t + 0 }')
	set -- $(nm_counts "$a")
	sT=$1 sD=$2 sU=$3
	set -- $(nm_counts "$b")
	rT=$1 rD=$2 rU=$3
	if readelf -S "$a" 2>/dev/null | grep -q '[.]stack_sizes'; then
		kA=$(readelf -x .stack_sizes "$a" 2>/dev/null | digest)
		kB=$(readelf -x .stack_sizes "$b" 2>/dev/null | digest)
	else
		kA=none
		kB=none
	fi
	dA=$(objdump -d "$a" 2>/dev/null | grep -v 'file format' | digest)
	dB=$(objdump -d "$b" 2>/dev/null | grep -v 'file format' | digest)
	if [ "$dA" = "$dB" ]; then dS=same; else dS=diff; fi
	if cmp -s "$a" "$b"; then status=same; else status=diff; fi
	printf '%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n' \
		"$o" "$status" "$bA" "$bB" "$tA" "$tB" "$sT" "$sD" "$sU" \
		"$rT" "$rD" "$rU" "$kA" "$kB" "$dS"
}

while [ $# -gt 0 ]; do
	case $1 in
	--jobs) [ $# -ge 2 ] || die "--jobs needs a count"; JOBS=$2; shift ;;
	--ko) [ $# -ge 2 ] || die "--ko needs a directory"; KO=$2; shift ;;
	--ref) [ $# -ge 2 ] || die "--ref needs a directory"; REF=$2; shift ;;
	--one) [ $# -ge 2 ] || die "--one needs an object"; ONE=$2; shift ;;
	-h|--help) usage; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

HASH=$(hash_cmd)
[ -n "$HASH" ] || die "no SHA-256 or MD5 tool (install coreutils)"
export KO REF

if [ -n "$ONE" ]; then
	one_object "$ONE"
	exit 0
fi

for t in cmp size nm objdump readelf awk wc tr xargs sort mktemp; do
	command -v "$t" >/dev/null 2>&1 || die "missing tool: $t (install binutils and coreutils)"
done
[ -f "$CORPUS" ] || die "missing corpus list $CORPUS"
[ -d "$KO" ] || die "no Crug object directory at $KO (build it: scripts/kernel-stage.sh K4 K5, then scripts/kernel-object-diff.sh)"
[ -d "$REF" ] || die "no reference object directory at $REF (build it: scripts/kernel-stage.sh K4 K5, then scripts/kernel-object-diff.sh)"
mkdir -p "$WORK" || die "cannot create scratch directory $WORK (check permissions)"
# Private scratch directory per run: concurrent invocations (or a stale run)
# cannot truncate each other's record files.
RUN=$(mktemp -d "$WORK/run.XXXXXX") || die "cannot create a scratch directory in $WORK (check permissions)"
trap 'rm -rf "$RUN"' 0 1 2 15
LIST=$RUN/corpus.txt
RECORDS=$RUN/records.txt

awk '/^<!-- corpus:begin -->[ \t]*$/ { on = 1; next }
     /^<!-- corpus:end -->[ \t]*$/   { on = 0; next }
     on && $0 ~ /^[A-Za-z0-9_.\/-]+[.]o$/ { print }' "$CORPUS" >"$LIST" \
	|| die "cannot read $CORPUS"
total=$(wc -l <"$LIST" | tr -d ' ')
[ "$total" -gt 0 ] || die "$CORPUS has no corpus objects between corpus:begin and corpus:end"

say "comparing $total object(s): $KO against $REF ($JOBS job(s))"
if ! xargs -P "$JOBS" -n 1 sh "$SELF" --one <"$LIST" >"$RECORDS"; then
	die "the per-object analysis failed (see the messages above)"
fi
[ -s "$RECORDS" ] || die "the per-object analysis produced no records"
sorted=$RUN/records.sorted
LC_ALL=C sort "$RECORDS" >"$sorted" || die "cannot sort the analysis records"

identical=0
explained=0
differing=0
missing=0
bytes=0
text=0
symbols=0
sdef=0
sund=0
disdiff=0
sizediff=0
textdiff=0
symdiff=0
stackobjs=0
stackdiff=0

while read -r o status bA bB tA tB sT sD sU rT rD rU kA kB dS; do
	case $status in
	same|diff)
		case $bA:$tA:$sT:$rT in
		[0-9]*:[0-9]*:[0-9]*:[0-9]*) ;;
		*) die "malformed analysis record for $o" ;;
		esac
		;;
	missing)
		warn "missing corpus object: $o"
		missing=$((missing + 1))
		continue
		;;
	*) die "malformed analysis record for $o" ;;
	esac
	bytes=$((bytes + bA))
	text=$((text + tA))
	symbols=$((symbols + sT))
	sdef=$((sdef + sD))
	sund=$((sund + sU))
	[ "$bA" = "$bB" ] || sizediff=$((sizediff + 1))
	[ "$tA" = "$tB" ] || textdiff=$((textdiff + 1))
	if [ "$sT" != "$rT" ] || [ "$sD" != "$rD" ] || [ "$sU" != "$rU" ]; then
		symdiff=$((symdiff + 1))
	fi
	if [ "$kA" != none ]; then
		stackobjs=$((stackobjs + 1))
		[ "$kA" = "$kB" ] || stackdiff=$((stackdiff + 1))
	fi
	if [ "$dS" = diff ]; then
		disdiff=$((disdiff + 1))
	fi
	if [ "$status" = same ]; then
		if [ "$dS" = diff ]; then
			warn "byte-identical but disassembly differs: $o"
			differing=$((differing + 1))
		else
			identical=$((identical + 1))
		fi
		continue
	fi
	reason=$(allow_reason "$o")
	if [ -n "$reason" ]; then
		say "explained: $o: $reason"
		explained=$((explained + 1))
	else
		warn "unexplained difference: $o"
		differing=$((differing + 1))
	fi
done <"$sorted"

[ "$missing" -eq 0 ] || die "$missing corpus object(s) missing (rebuild with scripts/kernel-stage.sh K4 K5 and scripts/kernel-object-diff.sh)"

say "disassembly: $disdiff of $total object(s) differ"
say "file size: $sizediff of $total object(s) differ; .text size: $textdiff differ"
say "symbols: $symbols total ($sdef defined, $sund undefined), $symdiff object(s) differ"
if [ "$stackobjs" -eq 0 ]; then
	say "stack-usage: no .stack_sizes section in the corpus objects"
else
	say "stack-usage: $stackobjs object(s) carry .stack_sizes, $stackdiff differ"
fi
art=
if [ -f "$KO/arch/x86/boot/bzImage" ]; then
	art="bzImage $(wc -c <"$KO/arch/x86/boot/bzImage" | tr -d ' ') bytes"
fi
if [ -f "$KO/vmlinux" ]; then
	art="${art:+$art, }vmlinux $(wc -c <"$KO/vmlinux" | tr -d ' ') bytes"
fi
[ -z "$art" ] || say "full build: $art"

say "$total object(s): $identical identical, $explained explained, $differing differing (bytes $bytes, text $text, symbols $symbols)"

[ "$differing" -eq 0 ] || {
	warn "$differing unexplained object difference(s); record each in kernel/object-diff-allow.txt"
	exit 1
}
exit 0
