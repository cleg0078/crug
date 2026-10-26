#!/bin/sh
# check-p0-open.sh - validate the CRUG known-issue ledger and count open P0s.
#
# docs/crug/KNOWN-ISSUES.md is the machine-readable record that the phase-19,
# phase-22, phase-24 and phase-25 gates refer to. This script parses its ledger
# region, validates every row's shape and answers two questions:
#
#   - how many P0 entries are still open (all scopes, or kernel scope only)?
#   - do all fixed P0/P1 entries name an existing regression test?
#
# The ledger region must be delimited by exactly one ledger:begin line and one
# later ledger:end line. Inside it the first non-empty line must be the header
# '# id severity scope status regression summary'; every following non-comment
# line must have all six fields, a K-<digits> id (unique), a known severity,
# scope and status, a '-' regression only while open, and an existing
# repository-relative regression file for every entry that names one. A
# malformed ledger is a gate error, not an empty count: the phase gates must
# never pass because the evidence container disappeared.
#
# Usage:
#   scripts/check-p0-open.sh                    all scopes (phase 22/24/25)
#   scripts/check-p0-open.sh --kernel           kernel scope only (phase 19)
#   scripts/check-p0-open.sh --ledger FILE      parse FILE instead (self-test)
#
# Exit status: 0 no open P0 (and every fixed P0/P1 has an existing regression
# test), 1 an open P0 or a fixed P0/P1 without a regression test, 2 a malformed
# ledger or a usage error.
set -u -f

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=check-p0-open
LEDGER=$ROOT/docs/crug/KNOWN-ISSUES.md
scope_filter=''

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--kernel) scope_filter=kernel ;;
	--ledger)
		shift
		[ $# -gt 0 ] || die '--ledger needs a file'
		LEDGER=$1
		;;
	--ledger=*) LEDGER=${1#--ledger=} ;;
	-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

[ -f "$LEDGER" ] || die "missing $LEDGER"

tmp=$(mktemp -d) || die 'cannot create a temporary directory'
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

begin_count=$(grep -c '^<!-- ledger:begin -->[[:space:]]*$' "$LEDGER" || true)
end_count=$(grep -c '^<!-- ledger:end -->[[:space:]]*$' "$LEDGER" || true)
[ "$begin_count" -eq 1 ] || die "expected exactly one ledger:begin marker, found $begin_count"
[ "$end_count" -eq 1 ] || die "expected exactly one ledger:end marker, found $end_count"
begin_line=$(awk '/^<!-- ledger:begin -->[ \t]*$/{print NR; exit}' "$LEDGER")
end_line=$(awk '/^<!-- ledger:end -->[ \t]*$/{print NR; exit}' "$LEDGER")
[ "$begin_line" -lt "$end_line" ] || die 'ledger:end appears before ledger:begin'

sed -n "$((begin_line + 1)),$((end_line - 1))p" "$LEDGER" >"$tmp/region"

header=$(grep -v '^[[:space:]]*$' "$tmp/region" | head -n 1)
printf '%s\n' "$header" | awk '
	NF != 7 || $1 != "#" || $2 != "id" || $3 != "severity" || $4 != "scope" \
		|| $5 != "status" || $6 != "regression" || $7 != "summary" { exit 1 }' \
	|| die 'ledger header must be: # id severity scope status regression summary'

awk '
	/^[[:space:]]*#/ { next }
	/^[[:space:]]*$/ { next }
	{ print }' "$tmp/region" >"$tmp/rows"

bad=0
open_p0=0
fixed_missing=0
seen=''

is_id () {
	case $1 in K-*) ;; *) return 1 ;; esac
	n=${1#K-}
	case $n in ''|*[!0-9]*) return 1 ;; esac
	return 0
}

while IFS= read -r line; do
	[ -n "$line" ] || continue
	set -- $line
	if [ $# -lt 6 ]; then
		warn "malformed ledger line (need 6 fields): $line"
		bad=1
		continue
	fi
	id=$1 sev=$2 scope=$3 status=$4 regression=$5
	if ! is_id "$id"; then warn "bad id in: $line"; bad=1; continue; fi
	case $sev in P0|P1|P2|P3) ;; *) warn "bad severity in: $line"; bad=1; continue ;; esac
	case $scope in
		kernel|runtime|compiler|ffi|docs|ci) ;;
		*) warn "bad scope in: $line"; bad=1; continue ;;
	esac
	case $status in open|fixed|wontfix) ;; *) warn "bad status in: $line"; bad=1; continue ;; esac
	case " $seen " in *" $id "*) warn "duplicate id: $id"; bad=1 ;; esac
	seen="$seen $id"
	case $regression in
	-) : ;;
	/*|*..*|*[!A-Za-z0-9._/+@:-]*)
		warn "bad regression path in: $line"
		bad=1
		continue
		;;
	*)
		if [ ! -f "$ROOT/$regression" ]; then
			warn "regression does not exist: $regression (in $id)"
			bad=1
			continue
		fi
		;;
	esac
	if [ "$status" = open ] && [ "$regression" != '-' ]; then
		warn "open entry $id must use '-' as its regression field"
		bad=1
	fi
	if [ -n "$scope_filter" ] && [ "$scope" != "$scope_filter" ]; then
		continue
	fi
	if [ "$sev" = P0 ] && [ "$status" = open ]; then
		warn "open P0: $id ($scope): $line"
		open_p0=$((open_p0 + 1))
	fi
	if { [ "$sev" = P0 ] || [ "$sev" = P1 ]; } && [ "$status" = fixed ] \
			&& [ "$regression" = '-' ]; then
		warn "fixed $sev without a regression id: $id"
		fixed_missing=$((fixed_missing + 1))
	fi
done <"$tmp/rows"

[ "$bad" -eq 0 ] || die 'the ledger is malformed'

if [ -n "$scope_filter" ]; then
	say "$open_p0 open P0 in scope '$scope_filter' ($fixed_missing fixed P0/P1 without a regression id)"
else
	say "$open_p0 open P0 ($fixed_missing fixed P0/P1 without a regression id)"
fi
[ "$open_p0" -eq 0 ] && [ "$fixed_missing" -eq 0 ] && exit 0
exit 1
