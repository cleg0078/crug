#!/bin/sh
# run-coverage.sh - write the immutable GCOV source-coverage archive for HEAD.
#
# The archive is the provenance record that scripts/check-coverage.sh reads.
# It is written to docs/crug/coverage/<full-commit>.json and contains:
#   - the HEAD commit the measurement describes;
#   - the SHA-256 of the GCOV cc1 that produced the profiles;
#   - the SHA-256 of the representative corpus manifest;
#   - one row per measured .cc/.gcno/.gcda set, with both file hashes and the
#     line and function counters GCOV reported;
#   - the totals recomputed from those rows;
#   - the compilation count and verdicts of the reproducible corpus.
#
# The measurement inputs are produced outside the repository by the audit
# harness (fuzz/scripts/run_gcov_corpus.py and fuzz/scripts/gcov_measure.py).
# This script never invents counters: if an input is missing it exits 2.
#
# Usage:
#   scripts/run-coverage.sh [--measure FILE] [--profiles FILE] [--cc1 FILE]
#                           [--corpus FILE] [--out DIR] [--force]
#   scripts/run-coverage.sh --self-test
#
# The measurement, profiles and corpus inputs must each declare the measured
# revision in a top-level "commit" field. A missing field is exit 2: an
# archive must never carry HEAD's label when its inputs do not declare what
# they measured.
#
# --self-test builds synthetic inputs in a temporary directory and proves that
# an absent commit field is rejected for each of the three inputs (exit 2, the
# message names the input), that a mismatching commit is rejected (exit 1) and
# that matching inputs still write the archive for HEAD.
#
# Defaults:
#   --measure   fuzz/gcov-report/crug-source-coverage.json
#   --profiles  fuzz/gcov-report/final-profiles/manifest.json
#   --cc1       fuzz/gcov-build/gcc/cc1
#   --corpus    fuzz/gcov-report/representative-corpus.json
#   --out       docs/crug/coverage
#
# Exit: 0 archive written (or already identical and untouched), 1 an input
# declares a different commit or an archive for this commit already exists,
# 2 a measurement input is missing, invalid or does not declare its commit.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SELF=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/$(basename -- "$0")
cd "$ROOT" || exit 2
NAME=run-coverage
measure=fuzz/gcov-report/crug-source-coverage.json
profiles=fuzz/gcov-report/final-profiles/manifest.json
cc1=fuzz/gcov-build/gcc/cc1
corpus=fuzz/gcov-report/representative-corpus.json
outdir=docs/crug/coverage
force=0
self_test=0

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

# selftest proves the provenance contract: the three measurement inputs must
# each declare the commit they measured. It runs this same script against
# synthetic inputs in a private temporary directory.
selftest () {
	local tmp commit failures=0 label want needle out rc
	tmp=$(mktemp -d) || { warn 'self-test: cannot create a temporary directory'; return 1; }
	# shellcheck disable=SC2064
	trap "rm -rf '$tmp'" EXIT HUP INT TERM
	commit=$(git rev-parse HEAD) || { warn 'self-test: cannot resolve HEAD'; return 1; }
	printf 'synthetic cc1\n' >"$tmp/cc1" || { warn 'self-test: cannot write fixture'; return 1; }
	python3 - "$tmp" "$commit" <<'PY' || return 1
import json, os, sys

tmp, commit = sys.argv[1], sys.argv[2]

def fixtures(c):
    return {
        'measure': {'commit': c, 'files': [{
            'path': 'src/a.c',
            'lines': {'executed': 1, 'instrumented': 2},
            'functions': {'executed': 1, 'instrumented': 1}}]},
        'profiles': {'commit': c, 'files': [{
            'path': 'src/a.c',
            'gcno_sha256': 'a' * 64, 'gcda_sha256': 'b' * 64}]},
        'corpus': {'commit': c, 'total': 2, 'accepted': 1, 'rejected': 1},
    }

def emit(case, fx):
    for name, obj in fx.items():
        with open(os.path.join(tmp, '%s-%s.json' % (name, case)), 'w') as fh:
            json.dump(obj, fh)

emit('ok', fixtures(commit))
for name in ('measure', 'profiles', 'corpus'):
    fx = fixtures(commit)
    del fx[name]['commit']
    emit(name + '-absent', fx)
    fx = fixtures(commit)
    fx[name]['commit'] = 'deadbeef'
    emit(name + '-mismatch', fx)
PY
	run_case () {
		label=$1 want=$2 needle=$3
		out=$("$SELF" --measure "$tmp/measure-$label.json" \
			--profiles "$tmp/profiles-$label.json" --cc1 "$tmp/cc1" \
			--corpus "$tmp/corpus-$label.json" --out "$tmp/out-$label" 2>&1)
		rc=$?
		if [ "$rc" -ne "$want" ]; then
			warn "self-test: $label: exit $rc, wanted $want"
			printf '%s\n' "$out" | sed 's/^/    /' >&2
			failures=$((failures + 1))
			return 1
		fi
		if [ -n "$needle" ] && ! printf '%s\n' "$out" | grep -qF -- "$needle"; then
			warn "self-test: $label: output does not contain: $needle"
			printf '%s\n' "$out" | sed 's/^/    /' >&2
			failures=$((failures + 1))
			return 1
		fi
		return 0
	}
	run_case ok 0 'archived'
	run_case measure-absent 2 'measurement input does not declare'
	run_case profiles-absent 2 'profiles input does not declare'
	run_case corpus-absent 2 'corpus input does not declare'
	run_case measure-mismatch 1 'measurement describes commit deadbeef'
	run_case profiles-mismatch 1 'profiles describes commit deadbeef'
	run_case corpus-mismatch 1 'corpus describes commit deadbeef'
	if ! python3 - "$tmp/out-ok/$commit.json" "$commit" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    archive = json.load(fh)
if archive.get('commit') != sys.argv[2]:
    print('self-test: archive commit is %r, wanted %s' % (archive.get('commit'), sys.argv[2]),
          file=sys.stderr)
    sys.exit(1)
PY
	then
		warn 'self-test: the ok archive does not record HEAD'
		failures=$((failures + 1))
	fi
	if [ "$failures" -ne 0 ]; then
		warn "self-test: $failures case(s) failed"
		return 1
	fi
	say 'self-test: OK (7 cases: 3 absent-commit rejections, 3 mismatches, 1 archive)'
	return 0
}

while [ $# -gt 0 ]; do
	case $1 in
	--measure=*) measure=${1#--measure=} ;;
	--measure)
		shift; [ $# -gt 0 ] || die '--measure needs a file'; measure=$1 ;;
	--profiles=*) profiles=${1#--profiles=} ;;
	--profiles)
		shift; [ $# -gt 0 ] || die '--profiles needs a file'; profiles=$1 ;;
	--cc1=*) cc1=${1#--cc1=} ;;
	--cc1)
		shift; [ $# -gt 0 ] || die '--cc1 needs a file'; cc1=$1 ;;
	--corpus=*) corpus=${1#--corpus=} ;;
	--corpus)
		shift; [ $# -gt 0 ] || die '--corpus needs a file'; corpus=$1 ;;
	--out=*) outdir=${1#--out=} ;;
	--out)
		shift; [ $# -gt 0 ] || die '--out needs a directory'; outdir=$1 ;;
	--force) force=1 ;;
	--self-test) self_test=1 ;;
	-h|--help) sed -n '2,42p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

command -v python3 >/dev/null 2>&1 || die 'python3 not found'
command -v git >/dev/null 2>&1 || die 'git not found'

if [ "$self_test" -eq 1 ]; then
	selftest
	exit $?
fi

missing=
for f in "$measure" "$profiles" "$cc1" "$corpus"; do
	[ -f "$f" ] || missing="$missing $f"
done
if [ -n "$missing" ]; then
	warn "measurement input(s) missing:$missing"
	warn 'the GCOV corpus and profiles are produced by the audit harness; see'
	warn 'spec/fix-p1/COVERAGE.md for the reproduction commands.'
	exit 2
fi

commit=$(git rev-parse HEAD) || die 'cannot resolve HEAD'
mkdir -p "$outdir" || die "cannot create $outdir"
out=$outdir/$commit.json

if [ -e "$out" ] && [ "$force" -eq 0 ]; then
	warn "$out already exists; the archive is immutable"
	warn 'pass --force only when replacing evidence with a re-run on the same commit'
	exit 1
fi

python3 - "$measure" "$profiles" "$cc1" "$corpus" "$commit" "$out" <<'PY'
import hashlib, json, os, sys
from datetime import datetime, timezone

measure_p, profiles_p, cc1_p, corpus_p, commit, out = sys.argv[1:7]

def fail(msg, code=2):
    print('run-coverage: ' + msg, file=sys.stderr)
    sys.exit(code)

def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()

def load(path, label):
    try:
        with open(path) as fh:
            return json.load(fh)
    except (OSError, ValueError) as exc:
        fail('cannot read %s: %s' % (label, exc))

def as_int(value, label):
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        fail('%s is not a non-negative integer: %r' % (label, value))
    return value

def pair(row, keys, label):
    nested = row.get(keys[0])
    if isinstance(nested, dict):
        ex = nested.get('executed')
        ins = nested.get('instrumented')
    else:
        ex = row.get(keys[1])
        ins = row.get(keys[2])
    return as_int(ex, label + ' executed'), as_int(ins, label + ' instrumented')

measure = load(measure_p, 'measurement file')
profiles = load(profiles_p, 'profiles manifest')
corpus = load(corpus_p, 'corpus manifest')

# Every input must declare the revision it measured. An absent field used to
# pass, which let an archive carry HEAD's label without its inputs declaring
# the measured revision (CR-03). Missing => exit 2 naming the input;
# present-but-different => exit 1.
for label, obj in (('measurement', measure), ('profiles', profiles), ('corpus', corpus)):
    recorded = obj.get('commit') if isinstance(obj, dict) else None
    if not isinstance(recorded, str) or not recorded:
        fail('%s input does not declare the commit it measures '
             '(missing top-level "commit" field)' % label)
    if recorded != commit:
        fail('%s describes commit %s, not HEAD %s' % (label, recorded, commit), 1)

rows = None
if isinstance(measure, dict):
    for key in ('files', 'modules', 'sources'):
        value = measure.get(key)
        if isinstance(value, list):
            rows = value
            break
        if isinstance(value, dict):
            rows = []
            for path, row in sorted(value.items()):
                row = dict(row)
                row.setdefault('path', path)
                rows.append(row)
            break
if not isinstance(rows, list) or not rows:
    fail('measurement file has no files/modules list')

hashes = {}
prows = None
if isinstance(profiles, dict):
    for key in ('files', 'profiles', 'pairs'):
        if isinstance(profiles.get(key), (list, dict)):
            prows = profiles[key]
            break
if isinstance(prows, list):
    for row in prows:
        if isinstance(row, dict) and row.get('path'):
            hashes[row['path']] = row
elif isinstance(prows, dict):
    hashes = prows
elif isinstance(profiles, dict) and profiles.get('gcno_sha256'):
    hashes = {'*': profiles}

files = []
seen = set()
for row in rows:
    if not isinstance(row, dict):
        fail('measurement row is not an object: %r' % (row,))
    path = row.get('path') or row.get('file') or row.get('source')
    if not path:
        fail('measurement row has no path: %r' % (row,))
    if path in seen:
        fail('measurement lists %s twice' % path)
    seen.add(path)
    lines_e, lines_i = pair(row, ('lines', 'lines_executed', 'lines_instrumented'), path + ' lines')
    funcs_e, funcs_i = pair(row, ('functions', 'functions_executed', 'functions_instrumented'), path + ' functions')
    if lines_e > lines_i or funcs_e > funcs_i:
        fail('%s executes more lines/functions than it instruments' % path)
    h = hashes.get(path) or hashes.get('*') or {}
    gcno = row.get('gcno_sha256') or h.get('gcno_sha256')
    gcda = row.get('gcda_sha256') or h.get('gcda_sha256')
    if not gcno or not gcda:
        fail('%s has no .gcno/.gcda SHA-256 in the profiles manifest' % path)
    for name, value in (('gcno', gcno), ('gcda', gcda)):
        if len(value) != 64 or any(c not in '0123456789abcdefABCDEF' for c in value):
            fail('%s %s SHA-256 is malformed' % (path, name))
    files.append({
        'path': path,
        'gcno_sha256': gcno.lower(),
        'gcda_sha256': gcda.lower(),
        'lines': {'executed': lines_e, 'instrumented': lines_i},
        'functions': {'executed': funcs_e, 'instrumented': funcs_i},
    })

files.sort(key=lambda r: r['path'])
lines_e = sum(r['lines']['executed'] for r in files)
lines_i = sum(r['lines']['instrumented'] for r in files)
funcs_e = sum(r['functions']['executed'] for r in files)
funcs_i = sum(r['functions']['instrumented'] for r in files)

def count(name):
    value = corpus.get(name) if isinstance(corpus, dict) else None
    if value is None and isinstance(measure, dict):
        value = measure.get(name)
    return as_int(value, 'corpus ' + name) if value is not None else None

comp_total = count('total')
comp_accept = count('accepted')
comp_reject = count('rejected')
if comp_total is None and isinstance(corpus, dict):
    for key in ('compilations', 'results', 'runs', 'cases'):
        comps = corpus.get(key)
        if not isinstance(comps, list):
            continue
        comp_total = len(comps)
        comp_accept = sum(
            1 for c in comps
            if isinstance(c, dict) and (
                c.get('accepted') is True or
                str(c.get('status', '')).lower() in ('accept', 'accepted', 'ok')))
        comp_reject = sum(
            1 for c in comps
            if isinstance(c, dict) and
            str(c.get('status', '')).lower() in ('reject', 'rejected', 'error'))
        break
if comp_total is None:
    fail('corpus manifest has no compilation count')
if comp_accept is None or comp_reject is None:
    fail('corpus manifest has no accepted/rejected counts')
if comp_accept + comp_reject != comp_total:
    fail('corpus accepted+rejected does not equal total')

data = {
    'schema': 1,
    'kind': 'crug-gcov-coverage',
    'commit': commit,
    'generated_utc': datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ'),
    'cc1_sha256': sha256(cc1_p),
    'measurement_sha256': sha256(measure_p),
    'corpus_sha256': sha256(corpus_p),
    'profiles_manifest_sha256': sha256(profiles_p),
    'compilations': {'total': comp_total, 'accepted': comp_accept, 'rejected': comp_reject},
    'gcov_files': len(files),
    'files': files,
    'totals': {
        'files': len(files),
        'lines': {'executed': lines_e, 'instrumented': lines_i},
        'functions': {'executed': funcs_e, 'instrumented': funcs_i},
    },
}

tmp = out + '.tmp'
try:
    with open(tmp, 'w') as fh:
        json.dump(data, fh, indent=2, sort_keys=True)
        fh.write('\n')
    os.replace(tmp, out)
except OSError as exc:
    fail('cannot write %s: %s' % (out, exc))

print('run-coverage: archived %s at %s' % (commit, out))
print('run-coverage: %d files, %d/%d lines, %d/%d functions, %d compilations' % (
    len(files), lines_e, lines_i, funcs_e, funcs_i, comp_total))
PY

exit $?
