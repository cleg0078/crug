#!/bin/sh
# check-p0-open.sh - count open P0 defects in the CRUG known-issue ledger.
#
# docs/crug/KNOWN-ISSUES.md is the machine-readable record that the phase-19,
# phase-24 and phase-25 gates refer to. This script parses its ledger region and
# answers two questions:
#
#   - how many P0 entries are still open (all scopes, or kernel scope only)?
#   - do all fixed P0/P1 entries name a regression test?
#
# Usage:
#   scripts/check-p0-open.sh             all scopes (phase 24/25)
#   scripts/check-p0-open.sh --kernel    kernel scope only (phase 19)
#
# Exit status: 0 no open P0 (and every fixed P0/P1 has a regression id),
# 1 an open P0 or a fixed P0/P1 without a regression id, 2 a malformed ledger
# or a usage error.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=check-p0-open
LEDGER=$ROOT/docs/crug/KNOWN-ISSUES.md
scope_filter=

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--kernel) scope_filter=kernel ;;
	-h|--help) sed -n '2,20p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

[ -f "$LEDGER" ] || die "missing docs/crug/KNOWN-ISSUES.md"

rows=$(awk '
	/^<!-- ledger:begin -->[ \t]*$/ {on = 1; next}
	/^<!-- ledger:end -->[ \t]*$/   {on = 0; next}
	on && $0 !~ /^[ \t]*#/ && $0 !~ /^[ \t]*$/ {print}' "$LEDGER")

bad=0
open_p0=0
fixed_missing=0
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
	id=$1 sev=$2 scope=$3 status=$4 regression=$5
	case $id in K-*) ;; *) warn "bad id in: $line"; bad=1; continue ;; esac
	case $sev in P0|P1|P2|P3) ;; *) warn "bad severity in: $line"; bad=1; continue ;; esac
	case $status in open|fixed|wontfix) ;; *) warn "bad status in: $line"; bad=1; continue ;; esac
	case " $seen " in *" $id "*) warn "duplicate id: $id"; bad=1 ;; esac
	seen="$seen $id"
	if [ -n "$scope_filter" ] && [ "$scope" != "$scope_filter" ]; then
		continue
	fi
	if [ "$sev" = P0 ] && [ "$status" = open ]; then
		warn "open P0: $id ($scope): $line"
		open_p0=$((open_p0 + 1))
	fi
	if { [ "$sev" = P0 ] || [ "$sev" = P1 ]; } && [ "$status" = fixed ] \
			&& [ "$regression" = "-" ]; then
		warn "fixed $sev without a regression id: $id"
		fixed_missing=$((fixed_missing + 1))
	fi
done <<EOF
$rows
EOF

[ "$bad" -eq 0 ] || die "the ledger is malformed"

if [ -n "$scope_filter" ]; then
	say "$open_p0 open P0 in scope '$scope_filter' ($fixed_missing fixed P0/P1 without a regression id)"
else
	say "$open_p0 open P0 ($fixed_missing fixed P0/P1 without a regression id)"
fi
[ "$open_p0" -eq 0 ] && [ "$fixed_missing" -eq 0 ] && exit 0
exit 1
