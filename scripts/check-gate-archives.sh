#!/bin/sh
# check-gate-archives.sh - verify that every completed phase has a green gate.
#
# docs/crug/EXIT-GATES.md section 2 requires each phase gate to be archived
# under docs/crug/gates/phase-N/<commit>/. The Phase 25 gate ("all prior
# milestone blockers closed") reads those archives. This script replays the
# verdict of every archived log the same way scripts/gate.sh adjudicates it:
# an expectation that names "exit N" passes when the recorded exit status is N,
# an expectation that names no output passes only on exit 0 with no output, and
# anything else is MANUAL and not evidence.
#
# Usage:
#   scripts/check-gate-archives.sh [--last=N]
#
# Options:
#   --last=N  the last phase that must be archived and green (default 24, the
#             phase before the release candidate).
#
# Exit status: 0 every phase has at least one fully green archive, 1 a phase is
# missing an archive or has none that is green, 2 a usage or repository error.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=check-gate-archives
GATES=$ROOT/docs/crug/gates
last=24

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--last=*) last=${1#--last=} ;;
	--last)
		shift
		[ $# -gt 0 ] || die "--last needs a number"
		last=$1
		;;
	-h|--help) sed -n '2,24p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

case $last in
	''|*[!0-9]*) die "--last needs a non-negative number, got '$last'" ;;
esac

[ -d "$GATES" ] || die "missing docs/crug/gates"

# adjudicate_log FILE: prints nothing and exits 0 when the log passes, prints
# a one-word reason and exits 1 otherwise.
adjudicate_log () {
	log=$1
	header=$(sed -n '1p' "$log")
	rc=$(printf '%s\n' "$header" | sed -n 's/.* rc=\([0-9]*\)$/\1/p')
	[ -n "$rc" ] || { printf 'malformed'; return 1; }
	exp=$(sed -n '3p' "$log")
	exp=${exp#expected: }
	if printf '%s\n' "$exp" | grep -Eq '(^|[^[:alnum:]_])exit[[:space:]]+[0-9]+'; then
		want=$(printf '%s\n' "$exp" | awk '{ if (match($0, /(^|[^[:alnum:]_])exit[ \t]+[0-9]+/)) { s = substr($0, RSTART, RLENGTH); sub(/.*exit[ \t]+/, "", s); print s; exit } }')
		[ "$rc" = "$want" ] && return 0
		printf 'exit %s, wanted %s' "$rc" "$want"
		return 1
	fi
	if printf '%s\n' "$exp" | grep -Eqi 'empty|no output'; then
		body=$(awk 'f{print} /^---$/{f=1}' "$log")
		if [ "$rc" = 0 ] && [ -z "$(printf '%s' "$body" | tr -d '[:space:]')" ]; then
			return 0
		fi
		printf 'wanted empty output on exit 0'
		return 1
	fi
	printf 'manual expectation'
	return 1
}

missing=0
open=0
green=0
n=0
while [ "$n" -le "$last" ]; do
	phase=$n
	found=0
	green_here=0
	detail=
	for dir in "$GATES/phase-$phase"/*/; do
		[ -d "$dir" ] || continue
		found=$((found + 1))
		count=0
		failed=0
		why=
		for log in "$dir"*.log; do
			[ -f "$log" ] || continue
			count=$((count + 1))
			if ! reason=$(adjudicate_log "$log"); then
				failed=$((failed + 1))
				why="$why $(basename "$log" .log):$reason;"
			fi
		done
		if [ "$count" -gt 0 ] && [ "$failed" -eq 0 ]; then
			green_here=$((green_here + 1))
		elif [ "$count" -gt 0 ]; then
			detail="$detail $(basename "$dir"):$failed/$count failed"
		fi
	done
	if [ "$found" -eq 0 ]; then
		warn "phase-$phase has no archive under docs/crug/gates"
		missing=$((missing + 1))
	elif [ "$green_here" -eq 0 ]; then
		warn "phase-$phase has no green archive:$detail"
		open=$((open + 1))
	else
		green=$((green + 1))
	fi
	n=$((n + 1))
done

total=$((last + 1))
say "$total phase(s) required: $green green, $open open, $missing missing"
[ "$missing" -eq 0 ] && [ "$open" -eq 0 ] && exit 0
exit 1
