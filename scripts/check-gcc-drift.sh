#!/bin/sh
# check-gcc-drift: does this fork still apply to a newer upstream GCC tree?
#
# Fetches the upstream files the fork modifies, plus the files that carry the
# behavioural assumptions the fork depends on, at a chosen upstream revision,
# then checks three things:
#   1. every context line of every fork hunk still exists upstream;
#   2. every attachment symbol still exists upstream;
#   3. every behavioural pin still holds.
#
# Drift that is understood and accepted is recorded in
# docs/crug/gcc-drift-known.txt with a reason; anything else fails the check.
#
# Usage:
#   scripts/check-gcc-drift.sh [REF]      REF defaults to master
#   scripts/check-gcc-drift.sh --ref REF
#   scripts/check-gcc-drift.sh --refresh  refetch even when a cache exists
#
# The fetched files are cached under build/gcc-drift/<upstream-sha>/, so a
# repeated run works offline. Exit status: 0 clean (all drift known), 1 unknown
# drift or a missing symbol/pin, 2 network or usage error.
#
# This script measures source drift only. It does not build anything and it
# does not modify the repository.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

TAG=releases/gcc-16.2
if [ -n "$(printenv CRUG_VENDOR_TAG)" ]; then
    TAG=$(printenv CRUG_VENDOR_TAG)
fi
REF=master
refresh=0

while [ $# -gt 0 ]; do
    case $1 in
    --ref)
        if [ $# -lt 2 ]; then
            echo "check-gcc-drift: --ref needs a revision" >&2
            exit 2
        fi
        REF=$2
        shift 2
        ;;
    --ref=*)
        REF=$1
        REF=${REF#--ref=}
        shift
        ;;
    --refresh)
        refresh=1
        shift
        ;;
    -h|--help)
        sed -n '2,23p' "$0"
        exit 0
        ;;
    -*)
        echo "check-gcc-drift: unknown option: $1" >&2
        exit 2
        ;;
    *)
        REF=$1
        shift
        ;;
    esac
done

if ! git rev-parse --verify --quiet "$TAG" >/dev/null; then
    echo "check-gcc-drift: no such ref: $TAG" >&2
    exit 2
fi

if printf '%s' "$REF" | grep -Eq '^[0-9a-f]{40}$'; then
    SHA=$REF
else
    SHA=$(git ls-remote https://github.com/gcc-mirror/gcc "refs/heads/$REF" 2>/dev/null | awk 'NR == 1 { print $1 }')
    if [ -z "$SHA" ]; then
        SHA=$(git ls-remote https://github.com/gcc-mirror/gcc "$REF" 2>/dev/null | awk 'NR == 1 { print $1 }')
    fi
    if [ -z "$SHA" ]; then
        echo "check-gcc-drift: cannot resolve $REF at gcc-mirror/gcc (network?)" >&2
        exit 2
    fi
fi

CACHE=$ROOT/build/gcc-drift/$SHA
MANIFEST=$CACHE/manifest.tsv
KNOWN=$ROOT/docs/crug/gcc-drift-known.txt
mkdir -p "$CACHE" || exit 2

MODIFIED=$(git diff --name-only --diff-filter=M "$TAG"..HEAD -- gcc/)
EXTRA="libcpp/directives.cc gcc/c-family/c-gimplify.cc gcc/opts.cc gcc/doc/install.texi"
FILES=$(printf '%s\n%s\n' "$MODIFIED" "$EXTRA" | awk 'NF && !seen[$0]++')

echo "gcc-drift: base gcc-mirror/gcc@$SHA (ref $REF), cache $CACHE"

: > "$MANIFEST" || exit 2
for f in $FILES; do
    dest=$CACHE/$(printf '%s' "$f" | tr '/' '_')
    if [ -s "$dest" ] && [ "$refresh" -eq 0 ]; then
        :
    else
        url="https://raw.githubusercontent.com/gcc-mirror/gcc/$SHA/$f"
        if curl -fsSL --retry 2 --max-time 120 "$url" -o "$dest.part"; then
            mv "$dest.part" "$dest"
        else
            rm -f "$dest.part"
            if [ -s "$dest" ]; then
                echo "gcc-drift: warning: reusing cached $f" >&2
            else
                echo "gcc-drift: cannot fetch $url (no network and no cache)" >&2
                exit 2
            fi
        fi
    fi
    printf '%s\t%s\n' "$f" "$dest" >> "$MANIFEST"
done

python3 - "$TAG" "$MANIFEST" "$KNOWN" <<'PY'
import os
import subprocess
import sys

tag, manifest, known_path = sys.argv[1], sys.argv[2], sys.argv[3]

pairs = []
for line in open(manifest, encoding="utf-8"):
    line = line.rstrip("\n")
    if not line:
        continue
    path, dest = line.split("\t")
    pairs.append((path, dest))

paths = [p for p, _ in pairs]
texts = {}
for path, dest in pairs:
    with open(dest, encoding="utf-8", errors="replace") as fh:
        texts[path] = fh.read()
linesets = dict((p, set(x.strip() for x in texts[p].split("\n"))) for p in texts)

diff = subprocess.run(["git", "diff", "-U3", tag + "..HEAD", "--"] + paths,
                      check=True, capture_output=True, text=True).stdout

hunks = []
cur = None
for line in diff.split("\n"):
    if line.startswith("+++ b/"):
        cur = line[6:]
    elif line.startswith("@@"):
        hunks.append((cur, []))
    elif hunks and line.startswith(" "):
        text = line[1:]
        if text.strip():
            hunks[-1][1].append(text)

known = {}
if os.path.exists(known_path):
    for line in open(known_path, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) >= 2:
            known.setdefault(parts[0], set()).add(parts[1])

by_file = {}
known_hits = 0
unknown = []
clean_hunks = 0
for path, ctxs in hunks:
    missing = sorted(set(c.strip() for c in ctxs if c.strip() not in linesets.get(path, set())))
    if not missing:
        clean_hunks += 1
        continue
    by_file[path] = missing
    for c in missing:
        if c in known.get(path, set()):
            known_hits += 1
        else:
            unknown.append((path, c))

for path in sorted(by_file):
    print("  drift: %s: %d context line(s) missing" % (path, len(by_file[path])))
    for c in by_file[path]:
        mark = "known" if c in known.get(path, set()) else "UNKNOWN"
        print("    %-8s %s" % (mark, c[:120]))

symbols = [s.strip() for s in """
c_parser_peek_nth_token
c_parser_next_token_starts_declspecs
c_parser_next_tokens_start_declaration
c_parser_declaration_or_fndef
c_parser_declspecs
c_parser_statement_after_labels
c_parser_cast_expression
c_parser_sizeof_or_countof_expression
c_parser_alignof_expression
c_parser_postfix_expression
c_parse_file
start_decl
push_parm_decl
grokfield
finish_struct
start_function
c_common_handle_option
c_common_post_options
c_cpp_builtins
struct c_declspecs
C_COMMON_OBJS
C_AND_OBJC_OBJS
fplugin
""".strip().split("\n") if s.strip()]

blob = "\n".join(texts.values())
missing_symbols = [s for s in symbols if s not in blob]

pins = [
    ("libcpp/directives.cc", "pfile->cb.include (pfile, pfile->directive_line"),
    ("libcpp/directives.cc", "pfile->directive->name, fname, angle_brackets"),
    ("gcc/c-family/c-gimplify.cc", "bind = body;"),
    ("gcc/c/c-tree.h", "extern void mark_exp_read (tree);"),
    ("gcc/common.opt", "const char *aux_base_name"),
    ("gcc/c-family/c-opts.cc", "c_common_handle_option"),
    ("gcc/opts.cc", "option_flag_var"),
    ("gcc/doc/install.texi", "ISO C++14"),
]
pin_fail = [(p, n) for p, n in pins if n not in texts.get(p, "")]

for n in missing_symbols:
    print("  symbol missing: %s" % n)
for p, n in pin_fail:
    print("  pin broken: %s: %s" % (p, n[:100]))

print("gcc-drift: symbols %d/%d present" % (len(symbols) - len(missing_symbols), len(symbols)))
print("gcc-drift: pins %d/%d hold" % (len(pins) - len(pin_fail), len(pins)))
print("drift-summary: %d hunks on %d files, %d clean, %d drifted (%d known context line(s), %d unknown); symbols %d/%d; pins %d/%d"
      % (len(hunks), len(set(p for p, _ in hunks)), clean_hunks, len(by_file),
         known_hits, len(unknown),
         len(symbols) - len(missing_symbols), len(symbols),
         len(pins) - len(pin_fail), len(pins)))
sys.exit(1 if (unknown or missing_symbols or pin_fail) else 0)
PY

exit $?
