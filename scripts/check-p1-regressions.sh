#!/bin/sh
# check-p1-regressions.sh - run the regression set that pins the fixed P1
# (and P0) entries in docs/crug/KNOWN-ISSUES.md.
#
# The P1 workstream must not be its own only witness. This script runs the
# targeted suites that cover the closed ledger entries (K-001 multi-OS,
# K-002 ownership), the c2crug proof cross-checks, and the task/channel
# runtime suites, and it first proves that the two adjudicators still reject
# fail-open evidence:
#
#   - scripts/gate.sh --self-test rejects a MANUAL expectation, a missing
#     output marker and an unparsed CHECK line;
#   - scripts/check-p0-open.sh rejects a broken ledger container, a malformed
#     row, an open P0 and a fixed P0/P1 with no regression test.
#
# Those negative cases run against temporary copies of the real ledger, so
# the repository ledger is never edited.
#
# Usage:
#   scripts/check-p1-regressions.sh              run the full regression set
#   scripts/check-p1-regressions.sh --self-test  only the adjudicator tests
#
# Exit status: 0 every check passed, 1 at least one check failed, 2 a usage
# or setup error.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
NAME=check-p1-regressions
LEDGER=$ROOT/docs/crug/KNOWN-ISSUES.md
mode=full

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--self-test) mode=self-test ;;
	-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

[ -f "$LEDGER" ] || die "missing $LEDGER"
[ -f "$ROOT/scripts/check-p0-open.sh" ] || die 'missing scripts/check-p0-open.sh'
[ -f "$ROOT/scripts/gate.sh" ] || die 'missing scripts/gate.sh'

tmp=$(mktemp -d) || die 'cannot create a temporary directory'
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

passed=0
failed=0

run () {
	desc=$1
	shift
	printf '== %s\n' "$desc"
	if "$@"; then
		passed=$((passed + 1))
	else
		warn "FAILED: $desc"
		failed=$((failed + 1))
	fi
}

# expect_ledger LABEL RC FILE [TEXT] runs the validator on FILE and requires
# exit status RC and, when TEXT is given, that TEXT appears in the output.
expect_ledger () {
	label=$1 want=$2 file=$3 needle=${4:-}
	out=$(sh "$ROOT/scripts/check-p0-open.sh" --ledger "$file" 2>&1)
	rc=$?
	if [ "$rc" -ne "$want" ]; then
		warn "FAILED: $label (exit $rc, wanted $want)"
		printf '%s\n' "$out" | sed 's/^/    /' >&2
		failed=$((failed + 1))
		return 1
	fi
	if [ -n "$needle" ] && ! printf '%s\n' "$out" | grep -qF -- "$needle"; then
		warn "FAILED: $label (output does not contain: $needle)"
		printf '%s\n' "$out" | sed 's/^/    /' >&2
		failed=$((failed + 1))
		return 1
	fi
	passed=$((passed + 1))
	return 0
}

ledger_negatives () {
	awk '$1 == "K-002" { $2 = "P0"; $4 = "open"; $5 = "-" } { print }' \
		"$LEDGER" >"$tmp/open-p0.md"
	expect_ledger 'ledger reports an open P0' 1 "$tmp/open-p0.md" '1 open P0'

	sed 's/fixed/broken/' "$LEDGER" >"$tmp/bad-status.md"
	expect_ledger 'ledger rejects an unknown status' 2 "$tmp/bad-status.md" 'malformed'

	sed '/^<!-- ledger:begin -->/d' "$LEDGER" >"$tmp/no-begin.md"
	expect_ledger 'ledger rejects a missing container' 2 "$tmp/no-begin.md" 'ledger:begin'

	sed 's|scripts/check-multi-os-ports.sh|scripts/no-such-regression-test.sh|' \
		"$LEDGER" >"$tmp/no-regression-file.md"
	expect_ledger 'ledger rejects a missing regression file' 2 "$tmp/no-regression-file.md" 'does not exist'

	awk '$1 == "K-002" { $5 = "-" } { print }' "$LEDGER" >"$tmp/fixed-no-regression.md"
	expect_ledger 'ledger counts a fixed P1 without a regression' 1 \
		"$tmp/fixed-no-regression.md" '1 fixed P0/P1 without a regression id'
}

adjudicator_tests () {
	run 'gate adjudicator self-test' bash "$ROOT/scripts/gate.sh" --self-test
	run 'archive replay self-test' sh "$ROOT/scripts/check-gate-archives.sh" --self-test
	run 'ledger validator on the real ledger' sh "$ROOT/scripts/check-p0-open.sh"
	ledger_negatives
}

if [ "$mode" = self-test ]; then
	adjudicator_tests
	[ "$failed" -eq 0 ] || { warn "$failed adjudicator test(s) failed"; exit 1; }
	say "self-test: OK ($passed adjudicator test(s))"
	exit 0
fi

adjudicator_tests
run 'ownership regression for K-002' make check-OWNERSHIP
run 'drop shadowing and destruction' make check-DROP
run 'borrow lender joins and reborrow proof' make check-BORROW
run 'alias fact rejection and disjointness' make check-ALIAS_FACTS
run 'FFI contract validation' make check-FFI
run 'qualifier lookahead grammar' make check-GRAMMAR_DG
run 'strict and unsafe contexts' make check-STRICT_UNSAFE
run 'safe core types' make check-SAFE_TYPES
run 'multi-OS regression for K-001' make check-MULTI_OS
run 'c2crug proofs and conformance' make check-C2CRUG
run 'goroutine and task runtime tests' make check-GOROUTINES
run 'channel runtime tests' make check-CHANNELS

if [ "$failed" -ne 0 ]; then
	warn "$failed check(s) failed, $passed passed"
	exit 1
fi
say "$passed check(s) passed"
exit 0
