#!/bin/sh
# verify-upgrade: run the acceptance matrix after the GCC baseline moves.
#
# This is the definition of "the fork still works on this tree". It runs the
# local suites, the codegen-identity checks and the diagnostic registry, and
# prints one PASS/FAIL line per check. It does not run the phase gates unless
# asked, and it never edits the repository except where a check archives its
# own evidence (the phase gates and the baseline recorder do that by design).
#
# Usage:
#   scripts/verify-upgrade.sh [--build] [--gates] [--record-baseline]
#
# Options:
#   --build            rebuild xgcc first (make build)
#   --gates            also run scripts/gate.sh for phases 0 to 9
#   --record-baseline  re-run the gcc.dg baseline and archive it (hours)
#
# The default tier needs an existing build/obj and takes a few minutes; each
# check's full output is kept under build/verify-upgrade/<label>.log.
#
# Exit status: 0 when every check passed, 1 when one failed, 2 on usage.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

BUILD=0
GATES=0
RECORD=0

while [ $# -gt 0 ]; do
    case $1 in
    --build) BUILD=1 ;;
    --gates) GATES=1 ;;
    --record-baseline) RECORD=1 ;;
    -h|--help) sed -n '2,20p' "$0"; exit 0 ;;
    *) echo "verify-upgrade: unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

OUT=$ROOT/build/verify-upgrade
rm -rf "$OUT"
mkdir -p "$OUT" || exit 2

export PATH="$ROOT/gcc:$PATH"

PASSED=0
FAILED=0
FAILED_LIST=

run_check() {
    label=$1
    shift
    start=$(date +%s)
    "$@" >"$OUT/$label.log" 2>&1
    rc=$?
    end=$(date +%s)
    if [ "$rc" -eq 0 ]; then
        PASSED=$((PASSED + 1))
        printf 'PASS  %-26s %4ss\n' "$label" "$((end - start))"
        return 0
    fi
    FAILED=$((FAILED + 1))
    FAILED_LIST="$FAILED_LIST $label"
    printf 'FAIL  %-26s %4ss  rc %s\n' "$label" "$((end - start))" "$rc"
    tail -n 6 "$OUT/$label.log" | sed 's/^/      /'
    return 1
}

xgcc_version() {
    make -s xgcc-version 2>/dev/null | sed -n 's/.*(GCC) \([0-9][0-9.]*\).*/\1/p' | head -n 1
}

check_version() {
    xv=$(xgcc_version)
    pin=$(sed -n 's/^GCC_VERSION[[:space:]]*?=[[:space:]]*//p' Makefile | head -n 1)
    if [ -z "$xv" ]; then
        echo "no xgcc under build/obj; run make baseline, or pass --build"
        return 1
    fi
    echo "xgcc $xv; Makefile pin $pin"
    if [ -z "$pin" ]; then
        echo "cannot read GCC_VERSION from the Makefile"
        return 1
    fi
    if [ "$xv" != "$pin" ]; then
        echo "the build and the pin disagree: update GCC_VERSION, GCC_URL and docs/crug/gcc-baseline.sha512"
        return 1
    fi
}

check_baseline() {
    readme=docs/crug/baseline/phase0/README.md
    if [ ! -f "$readme" ]; then
        echo "no archived phase-0 baseline under docs/crug/baseline/phase0"
        return 1
    fi
    bv=$(sed -n 's/^- Compiler: xgcc (GCC) //p' "$readme" | head -n 1)
    xv=$(xgcc_version)
    echo "baseline recorded with xgcc $bv; current xgcc $xv"
    if [ -z "$bv" ]; then
        echo "cannot read the recorded compiler from $readme"
        return 1
    fi
    if [ "$bv" != "$xv" ]; then
        echo "re-record: make baseline-phase0, then review docs/crug/baseline/phase0/known-fail.txt"
        return 1
    fi
}

echo "verify-upgrade: tree at $(git rev-parse --short=12 HEAD) on $(uname -m), $(nproc) jobs"
echo

if [ "$BUILD" -eq 1 ]; then
    run_check build make -s build
fi

if [ "$RECORD" -eq 1 ]; then
    run_check record-baseline make -s baseline-phase0
    run_check baseline-validate scripts/check-baseline.sh docs/crug/baseline/phase0
fi

run_check version-pin check_version
run_check baseline-fresh check_baseline

run_check c-compat make -s check-C_COMPAT
run_check crug-flags scripts/check-crug-flags.sh
run_check gnu-c-compat scripts/check-gnu-c-compat.sh
run_check kernel-compat scripts/test-kernel-compat.sh

run_check ownership make -s check-OWNERSHIP
run_check drop-suite scripts/check-drop-suite.sh
run_check drop-exactly-once scripts/check-drop-exactly-once.sh
run_check drop-asan scripts/check-drop-asan.sh
run_check drop-objdump scripts/check-drop-objdump.sh
run_check drop-policy scripts/check-drop-policy.sh
run_check drop-dump scripts/check-drop-dump.sh

run_check borrow-suite scripts/check-borrow-suite.sh
run_check borrow-cost scripts/check-borrow-cost.sh
run_check borrow-abi scripts/check-borrow-abi.sh
run_check borrow-policy scripts/check-borrow-policy.sh
run_check borrow-dump scripts/check-borrow-dump.sh
run_check borrow-real-c scripts/check-borrow-real-c.sh
run_check borrow-escape scripts/check-borrow-escape.sh
run_check borrow-nll scripts/check-borrow-nll.sh

run_check strict-suite scripts/check-strict-suite.sh
run_check strict-raw scripts/check-strict-raw.sh
run_check strict-codegen scripts/check-strict-codegen.sh
run_check strict-policy scripts/check-strict-policy.sh

run_check ffi-suite scripts/check-ffi-suite.sh
run_check ffi-integration scripts/check-ffi-integration.sh
run_check ffi-conflict scripts/check-ffi-conflict.sh
run_check ffi-codegen scripts/check-ffi-codegen.sh
run_check ffi-policy scripts/check-ffi-policy.sh

run_check diagnostics scripts/check-diagnostics.sh

if [ "$GATES" -eq 1 ]; then
    for p in 0 1 2 3 4 5 6 7 8 9; do
        run_check "gate-phase-$p" scripts/gate.sh --run "phase-$p" --timeout 3600
    done
fi

printf '\nverify-upgrade: %d passed, %d failed (xgcc %s, tree %s)\n' \
    "$PASSED" "$FAILED" "$(xgcc_version)" "$(git rev-parse --short=12 HEAD)"

if [ "$FAILED" -ne 0 ]; then
    printf 'verify-upgrade: failed:%s\n' "$FAILED_LIST"
    exit 1
fi

exit 0
