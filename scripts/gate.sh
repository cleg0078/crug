#!/usr/bin/env bash
# Gate adjudicator for docs/crug/EXIT-GATES.md.
#
# Every CHECK line in that document has the form:
#   - 'CHECK: <command>' => <expected observable>
# This script extracts the commands, runs them from the repository root and
# classifies the result against the expectation. It never edits the gate
# document.
#
# An expectation is adjudicable only when it declares at least one of:
#   - exit N;
#   - empty output (also accepted: 'no output', 'printing nothing');
#   - a backticked marker after output, printed, printing, prints, print,
#     reports, reported, emits, summary line or log ends with.
# A marker may contain <n>, <N>, <bytes>, <revision>, <kb>, <sha> or
# <os>/<arch> placeholders, a trailing ... (any tail), or $(cat VERSION)
# (the repository VERSION, substituted without running a shell). Any other
# backticked text is a prose reference, not an observable.
#
# MANUAL (prose-only), unparsed CHECK lines and unknown markers are failures:
# they are not evidence and a phase gate must not pass on them.
#
# Usage:
#   scripts/gate.sh --list                 list phases and check counts
#   scripts/gate.sh --dry-run phase-0      validate and print, run nothing
#   scripts/gate.sh --run phase-0          run and archive one phase
#   scripts/gate.sh --run                  run every phase
#   scripts/gate.sh --self-test            run the held-out self-test
#
# Options:
#   --timeout N   per-check timeout in seconds (default 120)
#   --dry-run     print commands and validate expectations, do not run them
#
# Self-test:
#   Builds a temporary git repository with a four-check gate document holding
#   one passing check, one fail-open check (declared marker never printed),
#   one MANUAL check and one unparsed CHECK line. It must report checks: 4,
#   failed: 3 and exactly one PASS. Nothing is written into this repository.
#
# The repository's gcc/ directory is prepended to PATH, so the CHECK lines can
# write crugc as the gate document does; it resolves to gcc/crugc.
#
# Exit status: 0 when every selected check is adjudicated PASS, 1 when any
# check failed, was MANUAL, could not be parsed or timed out, 2 on usage or
# repository errors. A phase gate is archived under
# docs/crug/gates/phase-N/<short-commit>/NNN.log with a header that records
# the phase's expected check count, so the archive can be replayed.
set -uo pipefail

tick=$(printf '\140')
DOC_REL='docs/crug/EXIT-GATES.md'
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")

mode=list
dry=0
timeout_s=120
sel=()
while (($#)); do
  case $1 in
    --list) mode=list ;;
    --run) mode=run ;;
    --dry-run) dry=1 ;;
    --self-test) mode=self-test ;;
    --parse-expectation) mode=parse-expectation ;;
    --timeout) timeout_s=${2:-}; [[ $timeout_s =~ ^[0-9]+$ ]] || { echo 'gate.sh: --timeout needs a number' >&2; exit 2; }; shift ;;
    -h|--help) sed -n '2,50p' "$0"; exit 0 ;;
    -*) echo "gate.sh: unknown option $1" >&2; exit 2 ;;
    *) sel+=("$1") ;;
  esac
  shift
done

run_self_test() {
  local tmp out rc n pass fail manual unparsed logs ok=1
  tmp=$(mktemp -d) || { echo 'gate.sh: self-test: cannot create a temporary directory' >&2; return 1; }
  # shellcheck disable=SC2064
  trap "rm -rf '$tmp'" EXIT HUP INT TERM
  mkdir -p "$tmp/docs/crug" "$tmp/scripts"
  cp "$SELF" "$tmp/scripts/gate.sh"
  sed "s/@TICK@/$tick/g" >"$tmp/$DOC_REL" <<'FIXTURE'
# Gate self-test fixture

## Phase 99 - self-test

- @TICK@CHECK: printf 'ok: 1\\n'@TICK@ => exit 0, printing @TICK@ok: 1@TICK@
- @TICK@CHECK: printf 'no: 2\\n'@TICK@ => exit 0, printing @TICK@needed: 3@TICK@
- @TICK@CHECK: true@TICK@ => this prose-only expectation is MANUAL and must fail.
- @TICK@CHECK: true@TICK@ and no expected-observable delimiter
FIXTURE
  git -C "$tmp" init -q || { echo 'gate.sh: self-test: git init failed' >&2; return 1; }
  git -C "$tmp" add -A
  git -C "$tmp" -c user.email=gate@example.invalid -c user.name=gate commit -q -m 'gate self-test fixture' || {
    echo 'gate.sh: self-test: git commit failed' >&2
    return 1
  }
  ( cd "$tmp" && bash scripts/gate.sh --run phase-99 ) >"$tmp/out" 2>&1
  rc=$?
  out=$tmp/out
  if [ "$rc" -ne 1 ]; then
    echo "gate.sh: self-test: expected exit 1, got $rc" >&2
    ok=0
  fi
  if ! grep -q '^checks: 4, failed: 3, timed out: 0$' "$out"; then
    echo "gate.sh: self-test: missing 'checks: 4, failed: 3, timed out: 0'" >&2
    ok=0
  fi
  pass=$(grep -c '^PASS' "$out" || true)
  fail=$(grep -c '^FAIL' "$out" || true)
  manual=$(grep -c '^MANUAL' "$out" || true)
  unparsed=$(grep -c '^UNPARSED' "$out" || true)
  if [ "$pass" -ne 1 ]; then echo "gate.sh: self-test: expected exactly one PASS, got $pass" >&2; ok=0; fi
  if [ "$fail" -ne 1 ]; then echo "gate.sh: self-test: expected exactly one FAIL (fail-open marker), got $fail" >&2; ok=0; fi
  if [ "$manual" -ne 1 ]; then echo "gate.sh: self-test: expected exactly one MANUAL, got $manual" >&2; ok=0; fi
  if [ "$unparsed" -ne 1 ]; then echo "gate.sh: self-test: expected exactly one UNPARSED, got $unparsed" >&2; ok=0; fi
  logs=$(find "$tmp/docs/crug/gates/phase-99" -name '*.log' 2>/dev/null | wc -l | tr -d ' ')
  if [ "$logs" != 4 ]; then echo "gate.sh: self-test: expected 4 archived logs, got $logs" >&2; ok=0; fi
  n=$(find "$tmp/docs/crug/gates/phase-99" -name '001.log' 2>/dev/null | head -n 1)
  if [ -z "$n" ] || ! grep -q '^phase-99 #1 checks=4 rc=' "$n"; then
    echo 'gate.sh: self-test: archived log does not record checks=4' >&2
    ok=0
  fi
  if [ "$ok" -eq 1 ]; then
    echo 'gate.sh: self-test: OK (4 checks, 3 failures: fail-open marker, MANUAL, unparsed)'
    return 0
  fi
  echo 'gate.sh: self-test output:' >&2
  sed -n '1,120p' "$out" >&2
  return 1
}

if [ "$mode" = self-test ]; then
  run_self_test || exit 1
  exit 0
fi

ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || { echo 'gate.sh: not inside a git work tree' >&2; exit 2; }
DOC="$ROOT/$DOC_REL"
[[ -f $DOC ]] || { echo "gate.sh: missing $DOC_REL" >&2; exit 2; }
VERSION=''
if [[ -f $ROOT/VERSION ]]; then VERSION=$(<"$ROOT/VERSION"); fi

phase_ids() {
  awk '/^## Phase /{p=$3; gsub(/[^0-9]/,"",p); print p}' "$DOC"
}

check_lines() {
  awk -v T="$tick" '
    /^## Phase /{p=$3; gsub(/[^0-9]/,"",p); next}
    p != "" && index($0, "- " T "CHECK: ") == 1 { print p "\t" $0 }' "$DOC"
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

declare -a C_PHASE=() C_LINE=()
while IFS=$'\t' read -r ph line; do
  [ -n "$ph" ] || continue
  C_PHASE+=("$ph")
  C_LINE+=("$line")
done < <(check_lines)
nchecks=${#C_PHASE[@]}

expected_for_phase() {
  local want=$1 i count=0
  for ((i = 0; i < nchecks; i++)); do
    [[ ${C_PHASE[i]} == "$want" ]] && ((++count))
  done
  printf '%s' "$count"
}

if ((dry == 0)) && [[ $mode == list ]]; then
  printf '%-10s %s\n' 'phase' 'checks'
  while read -r p; do
    n=$(expected_for_phase "$p")
    printf '%-10s %s\n' "phase-$p" "$n"
  done < <(phase_ids)
  echo
  echo 'Run with: scripts/gate.sh --run [phase-N ...]'
  exit 0
fi

# parse_expectation EXP sets EXP_KIND (exit|empty|manual|bad), EXP_RC and
# MARKERS. It returns 1 when the expectation uses a substitution this script
# cannot adjudicate (for example $(date)); that is a parse failure, not PASS.
declare -a MARKERS=()
parse_expectation() {
  local exp=$1 rest pre m armed=0
  EXP_KIND=manual
  EXP_RC=''
  MARKERS=()
  if [[ $exp =~ (^|[^[:alnum:]_])exit[[:space:]]+([0-9]+) ]]; then
    EXP_KIND=exit
    EXP_RC=${BASH_REMATCH[2]}
  fi
  if [[ $exp =~ ([Ee]mpty[[:space:]]+output|[Nn]o[[:space:]]+output|[Pp]rinting[[:space:]]+nothing) ]]; then
    EXP_KIND=empty
  fi
  rest=$exp
  while [[ $rest == *"$tick"* ]]; do
    pre=${rest%%"$tick"*}
    rest=${rest#*"$tick"}
    if [[ $rest != *"$tick"* ]]; then
      EXP_KIND=bad
      return 1
    fi
    m=${rest%%"$tick"*}
    rest=${rest#*"$tick"}
    if ((armed == 1)) && [[ $pre =~ ^[[:space:]]*(,[[:space:]]*(and[[:space:]]+)?|and[[:space:]]+)?$ ]]; then
      MARKERS+=("$m")
    elif [[ $pre =~ (output|printed|printing|prints|print|reports|reported|emits|summary[[:space:]]+line|log[[:space:]]+ends[[:space:]]+with)[[:space:]]+(a[[:space:]]+|an[[:space:]]+|the[[:space:]]+)?$ ]]; then
      MARKERS+=("$m")
      armed=1
    else
      armed=0
    fi
  done
  if [[ $EXP_KIND == manual ]] && ((${#MARKERS[@]} == 0)); then
    return 0
  fi
  for m in "${MARKERS[@]}"; do
    if [[ $m == *'$('* && $m != *'$(cat VERSION)'* ]]; then
      EXP_KIND=bad
      return 1
    fi
  done
  return 0
}

# marker_regex MARKER prints an extended regular expression for grep -E.
marker_regex() {
  local m=$1 out='' i c rest tok
  if [[ $m == *'$(cat VERSION)'* ]]; then
    if [[ -z $VERSION ]]; then return 1; fi
    m=${m//'$(cat VERSION)'/$VERSION}
  fi
  if [[ $m == *'$('* ]]; then return 1; fi
  # One gate marker uses a bare N as a numeric meta-variable.
  m=$(printf '%s' "$m" | sed -e 's/ N / <num> /g' -e 's/^N /<num> /' -e 's/ N$/<num>/')
  m=${m//'<os>/<arch>'/'<hostpair>'}
  i=0
  while ((i < ${#m})); do
    c=${m:i:1}
    if [[ $c == '<' ]]; then
      rest=${m:i}
      if [[ $rest =~ ^\<([^>]*)\> ]]; then
        tok=${BASH_REMATCH[1]}
        case $tok in
          sha) out+='[0-9A-Fa-f]+' ;;
          n|N|num|bytes|revision|kb) out+='[0-9]+' ;;
          hostpair) out+='[^[:space:]]+' ;;
          *) out+='[^[:space:]]+' ;;
        esac
        i=$((i + ${#BASH_REMATCH[0]}))
        continue
      fi
    elif [[ $c == '.' ]]; then
      if [[ ${m:i:3} == '...' ]]; then
        out+='.*'
        i=$((i + 3))
        continue
      fi
    fi
    case $c in
      '.'|'^'|'$'|'+'|'('|')'|'['|']'|'{'|'}'|'|') out+="\\$c" ;;
      '\') out+='\' ;;
      *) out+=$c ;;
    esac
    i=$((i + 1))
  done
  printf '%s' "$out"
}

# --parse-expectation is the machine interface used by
# scripts/check-gate-archives.sh: it validates an archived expectation and
# prints 'exit N', 'empty', or one 'marker<TAB>original<TAB>ere' per declared
# marker. Exit 0 parsed, 3 unknown/MANUAL-like, 4 prose-only MANUAL.
if [[ $mode == parse-expectation ]]; then
  if ((${#sel[@]} != 1)); then
    echo 'gate.sh: --parse-expectation needs exactly one expectation' >&2
    exit 2
  fi
  if ! parse_expectation "${sel[0]}"; then
    echo 'bad'
    exit 3
  fi
  case $EXP_KIND in
    exit) echo "exit $EXP_RC" ;;
    empty) echo 'empty' ;;
    manual) echo 'manual'; exit 4 ;;
    *) echo 'bad'; exit 3 ;;
  esac
  for m in "${MARKERS[@]}"; do
    if ! re=$(marker_regex "$m"); then
      echo 'bad'
      exit 3
    fi
    printf 'marker\t%s\t%s\n' "$m" "$re"
  done
  exit 0
fi

# adjudicate OUT RC EXP sets VERDICT (PASS|FAIL|MANUAL|UNPARSED|TIMEOUT|PARSE-ERROR).
adjudicate() {
  local out=$1 rc=$2 exp=$3 m re
  VERDICT=MANUAL
  if ((rc == 124)); then
    VERDICT=TIMEOUT
    return 0
  fi
  if ! parse_expectation "$exp"; then
    VERDICT=PARSE-ERROR
    return 0
  fi
  case $EXP_KIND in
    exit)
      if ((rc == EXP_RC)); then VERDICT=PASS; else VERDICT=FAIL; fi
      ;;
    empty)
      if ((rc == 0)) && [[ -z ${out//[[:space:]]/} ]]; then VERDICT=PASS; else VERDICT=FAIL; fi
      ;;
    manual)
      VERDICT=MANUAL
      return 0
      ;;
    *)
      VERDICT=PARSE-ERROR
      return 0
      ;;
  esac
  if [[ $VERDICT == PASS ]] && ((${#MARKERS[@]})); then
    for m in "${MARKERS[@]}"; do
      if ! re=$(marker_regex "$m"); then
        VERDICT=PARSE-ERROR
        return 0
      fi
      if ! grep -Eq -e "$re" <<<"$out"; then
        VERDICT=FAIL
        return 0
      fi
    done
  fi
  return 0
}

total_fail=0
total_timeout=0
ran=0
expected_total=0
phase_fail=0

# EXIT-GATES.md section 4 defines crugc as gcc/crugc; make that name live.
export PATH="$ROOT/gcc:$PATH"

cur_ph=''
idx=0
phase_ran=0
phase_expected=0

finish_phase() {
  if [[ -n $cur_ph ]] && ((phase_ran != phase_expected)); then
    echo "gate.sh: phase-$cur_ph: expected $phase_expected check(s), processed $phase_ran" >&2
    phase_fail=1
  fi
}

for ((i = 0; i < nchecks; i++)); do
  ph=${C_PHASE[i]}
  line=${C_LINE[i]}
  want_phase "$ph" || continue
  if [[ $ph != "$cur_ph" ]]; then
    finish_phase
    cur_ph=$ph
    idx=0
    phase_ran=0
    phase_expected=$(expected_for_phase "$ph")
    expected_total=$((expected_total + phase_expected))
  fi
  ((++idx))
  ((++phase_ran))
  ((++ran))
  body=${line#"- ${tick}CHECK: "}
  delim="${tick} => "

  if ((dry == 1)); then
    if [[ $body != *"$delim"* ]]; then
      printf 'UNPARSED phase-%s #%d: %s\n' "$ph" "$idx" "$line" >&2
      phase_fail=1
      continue
    fi
    cmd=${body%%"$delim"*}
    exp=${body#*"$delim"}
    if parse_expectation "$exp"; then
      if [[ $EXP_KIND == manual ]]; then
        printf 'MANUAL  phase-%s #%d: %s\n' "$ph" "$idx" "$exp" >&2
        phase_fail=1
      else
        printf 'phase-%s #%d\n  cmd: %s\n  exp: %s\n' "$ph" "$idx" "$cmd" "$exp"
      fi
    else
      printf 'UNPARSED expectation phase-%s #%d: %s\n' "$ph" "$idx" "$exp" >&2
      phase_fail=1
    fi
    continue
  fi

  dir="$ROOT/docs/crug/gates/phase-$ph/$(git -C "$ROOT" rev-parse --short HEAD)"
  mkdir -p "$dir"
  log="$dir/$(printf '%03d' "$idx").log"

  if [[ $body != *"$delim"* ]]; then
    printf 'phase-%s #%d checks=%d rc=1\ncmd: (unparsed CHECK line)\nexpected: (unparsed)\n---\n%s\n' "$ph" "$idx" "$phase_expected" "$line" >"$log"
    VERDICT=UNPARSED
    total_fail=$((total_fail + 1))
    printf '%-9s phase-%-2s #%03d  %s\n' "$VERDICT" "$ph" "$idx" "${line:0:100}"
    continue
  fi
  cmd=${body%%"$delim"*}
  exp=${body#*"$delim"}
  # stdin is /dev/null: a check must never read the gate document, which is
  # what this loop itself reads; a booting guest would otherwise type the
  # following CHECK lines into its console. pipefail makes "cmd | grep -q" a
  # failure when cmd itself fails, not just when grep does.
  out=$(cd "$ROOT" && timeout "$timeout_s" bash -o pipefail -c "$cmd" </dev/null 2>&1)
  rc=$?
  printf 'phase-%s #%d checks=%d rc=%d\ncmd: %s\nexpected: %s\n---\n%s\n' "$ph" "$idx" "$phase_expected" "$rc" "$cmd" "$exp" "$out" >"$log"

  adjudicate "$out" "$rc" "$exp"
  case $VERDICT in
    FAIL|MANUAL|UNPARSED|PARSE-ERROR) total_fail=$((total_fail + 1)) ;;
    TIMEOUT) total_timeout=$((total_timeout + 1)) ;;
  esac
  printf '%-9s phase-%-2s #%03d  %s\n' "$VERDICT" "$ph" "$idx" "${cmd:0:100}"
done
finish_phase

if ((dry == 1)); then
  echo "dry run: $ran check(s), nothing executed"
  ((phase_fail == 0)) || exit 1
  exit 0
fi

if ((ran == 0)); then
  echo 'gate.sh: no checks selected' >&2
  exit 2
fi

if ((expected_total != ran)) || ((phase_fail != 0)); then
  echo "gate.sh: expected $expected_total check(s), executed $ran" >&2
  total_fail=$((total_fail + 1))
fi

echo
echo "checks: $ran, failed: $total_fail, timed out: $total_timeout"
echo "logs: docs/crug/gates/phase-*/$(git -C "$ROOT" rev-parse --short HEAD)/"
((total_fail + total_timeout == 0)) || exit 1
exit 0
