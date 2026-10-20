#!/usr/bin/env bash
# Gate adjudicator for docs/crug/EXIT-GATES.md.
#
# Every CHECK line in that document has the form:
#   - `CHECK: <command>` => <expected observable>
# This script extracts the commands, runs them from the repository root and
# classifies the result. It never edits the gate document.
#
# Usage:
#   scripts/gate.sh --list                 list phases and check counts
#   scripts/gate.sh --dry-run phase-0      print the commands of a phase
#   scripts/gate.sh --run phase-0          run and archive one phase
#   scripts/gate.sh --run                  run every phase
#
# Options:
#   --timeout N   per-check timeout in seconds (default 120)
#   --dry-run     print commands, do not run them
#
# The repository's gcc/ directory is prepended to PATH, so the CHECK lines can
# write `crugc` as the gate document does; it resolves to gcc/crugc.
#
# Exit status: 0 when no check failed, 1 when a check failed or timed out,
# 2 on usage or repository errors. MANUAL means the expected observable is
# prose and a human must adjudicate the captured output.
set -uo pipefail

tick=$(printf "\140")
DOC_REL="docs/crug/EXIT-GATES.md"
ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo "gate.sh: not inside a git work tree" >&2; exit 2; }
DOC="$ROOT/$DOC_REL"
[[ -f $DOC ]] || { echo "gate.sh: missing $DOC_REL" >&2; exit 2; }

mode=list
dry=0
timeout_s=120
sel=()
while (($#)); do
  case $1 in
    --list) mode=list ;;
    --run) mode=run ;;
    --dry-run) dry=1 ;;
    --timeout) timeout_s=${2:-}; [[ $timeout_s =~ ^[0-9]+$ ]] || { echo "gate.sh: --timeout needs a number" >&2; exit 2; }; shift ;;
    -h|--help) sed -n "2,24p" "$0"; exit 0 ;;
    -*) echo "gate.sh: unknown option $1" >&2; exit 2 ;;
    *) sel+=("$1") ;;
  esac
  shift
done

phase_ids() {
  awk '/^## Phase /{p=$3; gsub(/[^0-9]/,"",p); print p}' "$DOC"
}

check_lines() {
  awk -v T="$tick" '
    /^## Phase /{p=$3; gsub(/[^0-9]/,"",p); next}
    index($0, "- " T "CHECK: ") == 1 { print p "\t" $0 }' "$DOC"
}

want_phase() {
  local p
  ((${#sel[@]} == 0)) && return 0
  for p in "${sel[@]}"; do
    p=${p#phase-}
    [[ $p == "$1" ]] && return 0
  done
  return 1
}

if ((dry == 0)) && [[ $mode == list ]]; then
  printf "%-10s %s\n" "phase" "checks"
  while read -r p; do
    n=$(check_lines | awk -F'\t' -v w="$p" '$1 == w' | wc -l)
    printf "%-10s %s\n" "phase-$p" "$n"
  done < <(phase_ids)
  echo
  echo "Run with: scripts/gate.sh --run [phase-N ...]"
  exit 0
fi

total_fail=0
total_timeout=0
ran=0

# EXIT-GATES.md section 4 defines `crugc` as gcc/crugc; make that name live.
export PATH="$ROOT/gcc:$PATH"

while IFS=$'\t' read -r ph line; do
  want_phase "$ph" || continue
  body=${line#"- ${tick}CHECK: "}
  delim="${tick} => "
  if [[ $body != *"$delim"* ]]; then
    echo "gate.sh: unparsed CHECK line in phase-$ph: $line" >&2
    continue
  fi
  cmd=${body%%"$delim"*}
  exp=${body#*"$delim"}
  ((++ran))

  if ((dry == 1)); then
    printf "phase-%s #%d\n  cmd: %s\n  exp: %s\n" "$ph" "$ran" "$cmd" "$exp"
    continue
  fi

  dir="$ROOT/docs/crug/gates/phase-$ph/$(git -C "$ROOT" rev-parse --short HEAD)"
  mkdir -p "$dir"
  log="$dir/$(printf "%03d" "$ran").log"
  # stdin is /dev/null: a check must never read the gate document, which is
  # what this loop itself reads; a booting guest would otherwise type the
  # following CHECK lines into its console.
  out=$(cd "$ROOT" && timeout "$timeout_s" bash -c "$cmd" </dev/null 2>&1)
  rc=$?
  printf "phase-%s #%d rc=%d\ncmd: %s\nexpected: %s\n---\n%s\n" \
    "$ph" "$ran" "$rc" "$cmd" "$exp" "$out" >"$log"

  verdict=MANUAL
  if ((rc == 124)); then
    verdict=TIMEOUT
    ((++total_timeout))
  elif [[ $exp =~ (^|[^[:alnum:]_])exit[[:space:]]+([0-9]+) ]]; then
    if ((rc == ${BASH_REMATCH[2]})); then verdict=PASS; else verdict=FAIL; fi
  elif [[ $exp =~ ([Ee]mpty|no[[:space:]]+output) ]]; then
    if ((rc == 0)) && [[ -z ${out//[[:space:]]/} ]]; then verdict=PASS; else verdict=FAIL; fi
  fi
  [[ $verdict == FAIL ]] && ((++total_fail))
  printf "%-7s phase-%-2s #%03d  %s\n" "$verdict" "$ph" "$ran" "${cmd:0:100}"
done < <(check_lines)

if ((dry == 1)); then
  echo "dry run: $ran check(s), nothing executed"
  exit 0
fi

if ((ran == 0)); then
  echo "gate.sh: no checks selected" >&2
  exit 2
fi

echo
echo "checks: $ran, failed: $total_fail, timed out: $total_timeout"
echo "logs: docs/crug/gates/phase-*/$(git -C "$ROOT" rev-parse --short HEAD)/"
((total_fail + total_timeout == 0)) || exit 1
exit 0
