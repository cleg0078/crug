#!/bin/sh
# count-open-p1.sh - count open P1 defects and check their waivers.
#
# docs/crug/RELEASE-POLICY.md section 4 allows at most two open P1 findings at
# the release-candidate tag, each with an owner-signed waiver row in
# docs/crug/KNOWN-ISSUES.md. This script parses the ledger and the waiver table
# and enforces both halves of that rule.
#
# Usage:
#   scripts/count-open-p1.sh [--max=N]
#
# Options:
#   --max=N   the allowed number of open P1 findings (default 2, the
#             RELEASE-POLICY section 4 threshold); --max=0 is the 1.0 rule.
#
# Exit status: 0 every open P1 is within the limit and has a signed waiver,
# 1 the limit is exceeded or an open P1 has no waiver, 2 a malformed ledger,
# waiver table or usage.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=count-open-p1
LEDGER=$ROOT/docs/crug/KNOWN-ISSUES.md
max_p1=2

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--max=*) max_p1=${1#--max=} ;;
	--max)
		shift
		[ $# -gt 0 ] || die "--max needs a number"
		max_p1=$1
		;;
	-h|--help) sed -n '2,22p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

case $max_p1 in
	''|*[!0-9]*) die "--max needs a non-negative number, got '$max_p1'" ;;
esac

[ -f "$LEDGER" ] || die "missing docs/crug/KNOWN-ISSUES.md"

rows=$(awk '
	/^<!-- ledger:begin -->[ \t]*$/ {on = 1; next}
	/^<!-- ledger:end -->[ \t]*$/   {on = 0; next}
	on && $0 !~ /^[ \t]*#/ && $0 !~ /^[ \t]*$/ {print}' "$LEDGER")

waivers=$(awk '
	/^<!-- waivers:begin -->[ \t]*$/ {on = 1; next}
	/^<!-- waivers:end -->[ \t]*$/   {on = 0; next}
	on {print}' "$LEDGER")

bad=0
open_p1=0
open_ids=
seen=
while IFS= read -r line; do
	[ -n "$line" ] || continue
	# shellcheck disable=SC2086
	set -- $line
	if [ $# -lt 6 ]; then
		warn "malformed ledger line: $line"
		bad=1
		continue
	fi
	id=$1 sev=$2 scope=$3 status=$4
	case $id in K-*) ;; *) warn "bad id in: $line"; bad=1; continue ;; esac
	case $sev in P0|P1|P2|P3) ;; *) warn "bad severity in: $line"; bad=1; continue ;; esac
	case $status in open|fixed|wontfix) ;; *) warn "bad status in: $line"; bad=1; continue ;; esac
	case " $seen " in *" $id "*) warn "duplicate id: $id"; bad=1 ;; esac
	seen="$seen $id"
	if [ "$sev" = P1 ] && [ "$status" = open ]; then
		open_p1=$((open_p1 + 1))
		open_ids="$open_ids $id"
	fi
done <<EOF
$rows
EOF

[ "$bad" -eq 0 ] || die "the ledger is malformed"

unwaived=0
seen_waivers=
while IFS= read -r line; do
	case $line in
	'|'*) ;;
	*) continue ;;
	esac
	id=$(printf '%s\n' "$line" | awk -F'|' '{gsub(/[ \t]/,"",$2); print $2}')
	signed=$(printf '%s\n' "$line" | awk -F'|' '{gsub(/^[ \t]+|[ \t]+$/,"",$3); print $3}')
	date=$(printf '%s\n' "$line" | awk -F'|' '{gsub(/[ \t]/,"",$4); print $4}')
	case $id in K-[0-9]*) ;; *) continue ;; esac
	case " $seen_waivers " in *" $id "*) warn "duplicate waiver row for $id"; bad=1 ;; esac
	seen_waivers="$seen_waivers $id"
	case $date in
	????-??-??) ;;
	*) warn "waiver for $id has no ISO date: $line"; bad=1; continue ;;
	esac
	if [ -z "$signed" ] || [ "$signed" = "-" ]; then
		warn "waiver for $id is not signed: $line"
		bad=1
		continue
	fi
	case " $open_ids " in
	*" $id "*) ;;
	*) warn "waiver for $id does not name an open P1"; bad=1 ;;
	esac
done <<EOF
$waivers
EOF

[ "$bad" -eq 0 ] || die "the waiver table is malformed"

for id in $open_ids; do
	case " $seen_waivers " in
	*" $id "*) ;;
	*) warn "open P1 without a waiver row: $id"; unwaived=$((unwaived + 1)) ;;
	esac
done

waived=$(printf '%s\n' "$open_ids" | wc -w)
waived=$((waived - unwaived))
say "$open_p1 open P1 (max $max_p1, $waived waived)"
[ "$open_p1" -le "$max_p1" ] && [ "$unwaived" -eq 0 ] && exit 0
exit 1
