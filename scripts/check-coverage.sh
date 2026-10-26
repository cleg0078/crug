#!/bin/sh
# check-coverage.sh - validate the GCOV coverage archive for HEAD.
#
# The archive is written by scripts/run-coverage.sh. This script verifies that
# it is self-consistent and belongs to the current HEAD commit:
#   - the recorded commit equals `git rev-parse HEAD`;
#   - every measured path exists at that commit;
#   - every .gcno/.gcda entry carries a 64-hex SHA-256;
#   - the stored totals equal the sums of the per-file counters;
#   - the compilation total equals accepted plus rejected;
#   - docs/crug/TESTING.md carries a coverage ledger block with the same
#     commit, cc1 hash, file count, line totals and function totals.
#
# There is deliberately no minimum percentage: a threshold would turn a
# measurement into a target and reward gaming the corpus. The check is about
# provenance and internal consistency, not about how large the coverage is.
#
# Usage:
#   scripts/check-coverage.sh [--manifest FILE] [--coverage-md FILE]
#
# Defaults: docs/crug/coverage/<HEAD>.json and docs/crug/TESTING.md.
#
# Exit: 0 the archive and the document agree with HEAD; 1 an inconsistency or
# a stale archive; 2 a required file, ledger block or field is absent (the data
# is unavailable, never silently treated as green).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
NAME=check-coverage
manifest=
coverage_md=docs/crug/TESTING.md

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }

while [ $# -gt 0 ]; do
	case $1 in
	--manifest=*) manifest=${1#--manifest=} ;;
	--manifest)
		shift; [ $# -gt 0 ] || die '--manifest needs a file'; manifest=$1 ;;
	--coverage-md=*) coverage_md=${1#--coverage-md=} ;;
	--coverage-md)
		shift; [ $# -gt 0 ] || die '--coverage-md needs a file'; coverage_md=$1 ;;
	-h|--help) sed -n '2,34p' "$0"; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
	shift
done

command -v python3 >/dev/null 2>&1 || die 'python3 not found'
commit=$(git rev-parse HEAD) || die 'cannot resolve HEAD'
[ -n "$manifest" ] || manifest=docs/crug/coverage/$commit.json

if [ ! -f "$manifest" ]; then
	warn "missing coverage archive $manifest"
	warn 'run scripts/run-coverage.sh with the GCOV measurement inputs to create it'
	exit 2
fi
if [ ! -f "$coverage_md" ]; then
	warn "missing $coverage_md"
	warn 'the coverage document states the commit, cc1 hash and totals the archive must match'
	exit 2
fi

python3 - "$manifest" "$coverage_md" "$commit" <<'PY'
import json, re, subprocess, sys

manifest_p, md_p, head = sys.argv[1], sys.argv[2], sys.argv[3]

def fail(msg, code):
    print('check-coverage: ' + msg, file=sys.stderr)
    sys.exit(code)

def sha_ok(value, label):
    if not isinstance(value, str) or len(value) != 64 or any(
            c not in '0123456789abcdef' for c in value):
        fail('%s is not a lowercase 64-hex SHA-256: %r' % (label, value), 1)

def nonneg(value, label):
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        fail('%s is not a non-negative integer: %r' % (label, value), 1)
    return value

try:
    with open(manifest_p) as fh:
        data = json.load(fh)
except (OSError, ValueError) as exc:
    fail('cannot read %s: %s' % (manifest_p, exc), 2)

if not isinstance(data, dict):
    fail('coverage archive is not a JSON object', 2)
if data.get('schema') != 1 or data.get('kind') != 'crug-gcov-coverage':
    fail('coverage archive is not schema 1 crug-gcov-coverage', 2)

commit = data.get('commit')
if commit != head:
    fail('archive describes commit %r, HEAD is %s; regenerate it' % (commit, head), 1)

sha_ok(data.get('cc1_sha256'), 'cc1_sha256')
sha_ok(data.get('measurement_sha256'), 'measurement_sha256')
sha_ok(data.get('corpus_sha256'), 'corpus_sha256')
sha_ok(data.get('profiles_manifest_sha256'), 'profiles_manifest_sha256')

files = data.get('files')
if not isinstance(files, list) or not files:
    fail('archive has no files list', 2)
if nonneg(data.get('gcov_files'), 'gcov_files') != len(files):
    fail('gcov_files does not equal the number of file rows', 1)

seen = set()
sums = {'lines': [0, 0], 'functions': [0, 0]}
for i, row in enumerate(files):
    if not isinstance(row, dict):
        fail('file row %d is not an object' % i, 2)
    path = row.get('path')
    if not isinstance(path, str) or not path or path.startswith('/') or '..' in path.split('/'):
        fail('file row %d has an unsafe path: %r' % (i, path), 1)
    if path in seen:
        fail('duplicate file row for %s' % path, 1)
    seen.add(path)
    try:
        present = subprocess.run(['git', 'cat-file', '-e', '%s:%s' % (commit, path)],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    except OSError as exc:
        fail('cannot run git: %s' % exc, 2)
    if present != 0:
        fail('%s does not exist at %s' % (path, commit), 1)
    sha_ok(row.get('gcno_sha256'), path + ' gcno_sha256')
    sha_ok(row.get('gcda_sha256'), path + ' gcda_sha256')
    for key in ('lines', 'functions'):
        pair = row.get(key)
        if not isinstance(pair, dict):
            fail('%s has no %s counters' % (path, key), 2)
        ex = nonneg(pair.get('executed'), '%s %s.executed' % (path, key))
        ins = nonneg(pair.get('instrumented'), '%s %s.instrumented' % (path, key))
        if ex > ins:
            fail('%s executes more %s than it instruments' % (path, key), 1)
        sums[key][0] += ex
        sums[key][1] += ins

totals = data.get('totals')
if not isinstance(totals, dict):
    fail('archive has no totals', 2)
if nonneg(totals.get('files'), 'totals.files') != len(files):
    fail('totals.files does not equal the number of rows', 1)
for key in ('lines', 'functions'):
    pair = totals.get(key)
    if not isinstance(pair, dict):
        fail('totals has no %s' % key, 2)
    ex = nonneg(pair.get('executed'), 'totals.%s.executed' % key)
    ins = nonneg(pair.get('instrumented'), 'totals.%s.instrumented' % key)
    if [ex, ins] != sums[key]:
        fail('totals.%s %d/%d does not match the summed rows %d/%d' % (
            key, ex, ins, sums[key][0], sums[key][1]), 1)

comps = data.get('compilations')
if not isinstance(comps, dict):
    fail('archive has no compilation counts', 2)
total = nonneg(comps.get('total'), 'compilations.total')
accept = nonneg(comps.get('accepted'), 'compilations.accepted')
reject = nonneg(comps.get('rejected'), 'compilations.rejected')
if accept + reject != total:
    fail('accepted+rejected (%d) does not equal total (%d)' % (accept + reject, total), 1)

try:
    with open(md_p) as fh:
        text = fh.read()
except OSError as exc:
    fail('cannot read %s: %s' % (md_p, exc), 2)

block = re.search(r'<!--\s*coverage:begin\s*-->(.*?)<!--\s*coverage:end\s*-->',
                  text, re.S)
if not block:
    fail('%s has no <!-- coverage:begin --> ledger block' % md_p, 2)
fields = {}
for line in block.group(1).splitlines():
    line = line.strip()
    if not line or line.startswith('#'):
        continue
    key, sep, value = line.partition(':')
    if sep:
        fields[key.strip().lower()] = value.strip()

problems = []
for key in ('commit', 'cc1-sha256', 'gcov-files', 'lines', 'functions'):
    if key not in fields:
        fail('%s ledger block is missing %s' % (md_p, key), 2)

doc_commit = fields['commit']
if len(doc_commit) < 7 or not re.fullmatch(r'[0-9a-fA-F]+', doc_commit) or not commit.startswith(doc_commit.lower()):
    problems.append('document commit %r does not match %s' % (doc_commit, commit))
if fields['cc1-sha256'].lower() != data['cc1_sha256']:
    problems.append('document cc1 hash does not match the archive')
try:
    doc_files = int(fields['gcov-files'])
except ValueError:
    fail('%s ledger gcov-files is not an integer: %r' % (md_p, fields['gcov-files']), 2)
if doc_files != len(files):
    problems.append('document file count %d does not match the archive %d' % (
        doc_files, len(files)))
for key in ('lines', 'functions'):
    m = re.fullmatch(r'([0-9][0-9 ]*)\s*/\s*([0-9][0-9 ]*)', fields[key])
    if not m:
        fail('%s ledger %s is not "executed / instrumented": %r' % (
            md_p, key, fields[key]), 2)
    doc_pair = [int(m.group(1).replace(' ', '')),
                int(m.group(2).replace(' ', ''))]
    if doc_pair != sums[key]:
        problems.append('document %s %d/%d does not match the archive %d/%d' % (
            key, doc_pair[0], doc_pair[1], sums[key][0], sums[key][1]))
if 'corpus-sha256' in fields and fields['corpus-sha256'].lower() != data['corpus_sha256']:
    problems.append('document corpus hash does not match the archive')
if 'profiles-manifest-sha256' in fields and fields['profiles-manifest-sha256'].lower() != data['profiles_manifest_sha256']:
    problems.append('document profiles manifest hash does not match the archive')

if problems:
    for problem in problems:
        print('check-coverage: ' + problem, file=sys.stderr)
    sys.exit(1)

print('check-coverage: commit=%s files=%d lines=%d/%d functions=%d/%d (TESTING.md agrees)' % (
    commit[:12], len(files), sums['lines'][0], sums['lines'][1],
    sums['functions'][0], sums['functions'][1]))
PY

exit $?
