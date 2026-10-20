#!/bin/sh
# check-regressions.sh - require a regression test for every fixed P0/P1 defect.
#
# docs/crug/KNOWN-ISSUES.md is the machine-readable CRUG defect ledger. Its data
# region sits between two marker lines made of HTML comments:
#
#   <!-- ledger:begin -->
#   <!-- ledger:end -->
#
# and each data line has six space-separated fields:
#
#   <id> <severity> <scope> <status> <regression> <summary>
#
# This script is the phase-22 gate: a P0 or P1 entry whose status is fixed must
# name the test that pins the fix. The ledger is parsed exactly as
# scripts/check-p0-open.sh parses it - the same marker lines, the same ignored
# '#' comment and blank lines, the same six-field shape and the same id,
# severity and status validation - so the two gates can never disagree about
# what an entry is.
#
# The regression field must be
#
#   - a non-empty path relative to the repository root (not absolute, no '..'),
#   - an existing file, or an existing directory that contains at least one file.
#
# With --run every executable regular file named there is run from the
# repository root, and a non-zero exit status fails the gate.
#
# Usage:
#   scripts/check-regressions.sh          check that every regression path exists
#   scripts/check-regressions.sh --run    also run the executable ones
#
# Exit status: 0 every fixed P0/P1 entry has a usable regression test,
# 1 a regression path is missing or an executed test failed, 2 a malformed
# ledger or a usage error.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NAME=check-regressions
LEDGER=$ROOT/docs/crug/KNOWN-ISSUES.md
run_tests=0

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }
usage () { sed -n '2,/^set -u$/p' "$0" | sed '$d'; }

while [ $# -gt 0 ]; do
	case $1 in
	--run) run_tests=1 ;;
	-h|--help) usage; exit 0 ;;
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
fixed_total=0
missing=0
run_failed=0
seen=

# The ledger is split with the shell's word splitting, exactly like
# check-p0-open.sh; disable pathname expansion so a '*' in a summary cannot
# turn into a file name and change the field count.
set -f
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

	[ "$sev" = P0 ] || [ "$sev" = P1 ] || continue
	[ "$status" = fixed ] || continue
	fixed_total=$((fixed_total + 1))

	case $regression in
	-)
		warn "$id: fixed $sev entry has no regression test"
		missing=$((missing + 1))
		continue
		;;
	/*)
		warn "$id: regression path is absolute, not repository-relative: $regression"
		missing=$((missing + 1))
		continue
		;;
	*/../*|../*|*/..|..)
		warn "$id: regression path escapes the repository: $regression"
		missing=$((missing + 1))
		continue
		;;
	esac

	if [ ! -e "$ROOT/$regression" ]; then
		warn "$id: regression path does not exist: $regression"
		missing=$((missing + 1))
		continue
	fi
	if [ -d "$ROOT/$regression" ]; then
		if [ -z "$(find "$ROOT/$regression" -type f -print 2>/dev/null | head -n 1)" ]; then
			warn "$id: regression directory contains no files: $regression"
			missing=$((missing + 1))
			continue
		fi
	elif [ ! -f "$ROOT/$regression" ]; then
		warn "$id: regression path is neither a regular file nor a directory: $regression"
		missing=$((missing + 1))
		continue
	fi

	[ "$run_tests" -eq 1 ] || continue
	[ -f "$ROOT/$regression" ] && [ -x "$ROOT/$regression" ] || continue
	( cd "$ROOT" && "./$regression" ) >/dev/null 2>&1
	rc=$?
	if [ "$rc" -ne 0 ]; then
		warn "$id: regression test failed (exit $rc): $regression"
		run_failed=$((run_failed + 1))
	fi
done <<EOF
$rows
EOF
set +f

[ "$bad" -eq 0 ] || die "the ledger is malformed"

say "$fixed_total fixed P0/P1 entr(ies), $missing without a regression test"
[ "$missing" -eq 0 ] && [ "$run_failed" -eq 0 ] && exit 0
[ "$run_failed" -gt 0 ] && warn "$run_failed regression test(s) failed"
exit 1
