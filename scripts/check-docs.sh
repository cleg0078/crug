#!/bin/sh
# check-docs.sh - the Phase 23 documentation gate.
#
# Reads docs/crug/REQUIRED-DOC-SECTIONS.md and checks, for every row:
#   - the document exists and is not empty;
#   - its first non-empty line is a level-one heading;
#   - one of its first fifteen lines contains "Status:";
#   - every required "## " heading appears verbatim.
# It then scans every top-level document for an unqualified post-1.0 claim
# (manifest section 4): a paragraph that mentions a roadmap-section-32 term
# must also say that the item is absent or later.
#
# Usage: scripts/check-docs.sh [--list]
#   --list   print the manifest rows and exit.
#
# Exit status: 0 clean, 1 a violation, 2 a missing manifest or python3.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest="$root/docs/crug/REQUIRED-DOC-SECTIONS.md"

command -v python3 >/dev/null 2>&1 || {
  echo "check-docs: ERROR: python3 is not available" >&2
  exit 2
}
[ -f "$manifest" ] || {
  echo "check-docs: ERROR: missing $manifest" >&2
  exit 2
}

if [ "${1:-}" = "--list" ]; then
  python3 - "$manifest" <<'PY'
import re, sys
text = open(sys.argv[1], encoding="utf-8").read()
count = 0
for line in text.splitlines():
    m = re.match(r"\|\s*(docs/crug/[^|]+?)\s*\|\s*(.+?)\s*\|\s*$", line)
    if m:
        print(m.group(1), "=>", " ; ".join(s.strip() for s in m.group(2).split(";")))
        count += 1
print(f"check-docs: {count} manifest row(s)", file=sys.stderr)
PY
  exit $?
fi

exec python3 - "$root" "$manifest" <<'PY'
import pathlib, re, sys

root = pathlib.Path(sys.argv[1])
manifest_path = pathlib.Path(sys.argv[2])
problems = []
checked = 0

text = manifest_path.read_text(encoding="utf-8")
rows = []
for line in text.splitlines():
    m = re.match(r"\|\s*(docs/crug/[^|]+?)\s*\|\s*(.+?)\s*\|\s*$", line)
    if m:
        doc, sections = m.group(1), [s.strip() for s in m.group(2).split(";") if s.strip()]
        rows.append((doc, sections))

if not rows:
    print("check-docs: ERROR: the manifest has no rows")
    sys.exit(2)

def normal(s):
    return " ".join(s.split())

listed = {doc for doc, _ in rows}
for path in sorted((root / "docs/crug").glob("*.md")):
    rel = path.relative_to(root).as_posix()
    if rel not in listed:
        problems.append(f"{rel}: not listed in the manifest")

for doc, sections in rows:
    checked += 1
    path = root / doc
    if not path.is_file():
        problems.append(f"{doc}: missing document")
        continue
    body = path.read_text(encoding="utf-8", errors="replace")
    lines = body.splitlines()
    if not body.strip():
        problems.append(f"{doc}: empty document")
    first = next((l for l in lines if l.strip()), "")
    if not first.startswith("# "):
        problems.append(f"{doc}: first non-empty line is not a level-one heading")
    head = [l for l in lines[:15]]
    if not any("Status:" in l for l in head):
        problems.append(f"{doc}: no line containing 'Status:' in the first 15 lines")
    present = {normal(l[3:]) for l in lines if l.startswith("## ")}
    for section in sections:
        if normal(section[3:]) not in present:
            problems.append(f"{doc}: missing required section '{section}'")

# ---- post-1.0 claim scan (manifest section 4) ----
terms = [
    r"\bgenerics? stabilization\b",
    r"\btraits?\b",
    r"\bshared ownership\b",
    r"\bkernel ownership contracts?\b",
    r"\bRCU-aware\b",
    r"\bkernel task backends?\b",
    r"\bio_uring\b",
    r"\bgrowable stacks?\b",
    r"\bBSD(s)?\b",
    r"\bRISC-V\b",
    r"\bembedded profiles?\b",
    r"\bGPU\b",
    r"\bAI/HPC\b",
    r"\benhanced c2crug\b",
    r"\bLSP\b",
    r"\bformatters?\b",
    r"\bpackage ecosystem\b",
]
qualifier = re.compile(
    r"(?i)\b(no|not|never|future|post-1\.0|deferred|planned|later|beyond|"
    r"out of scope|not implemented|not supported|not in v1|not yet|someday|"
    r"absent|missing|does not|do not)\b"
)

def paragraphs(path):
    block, fence = [], False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.lstrip().startswith("```"):
            fence = not fence
            continue
        if fence:
            continue
        if line.strip():
            block.append(line)
        elif block:
            yield block
            block = []
    if block:
        yield block

for path in sorted((root / "docs/crug").glob("*.md")):
    rel = path.relative_to(root).as_posix()
    for block in paragraphs(path):
        prose = re.sub("`[^`]*`", "", " ".join(block))
        hits = [t for t in terms if re.search(t, prose)]
        if hits and not qualifier.search(prose):
            problems.append(
                f"{rel}: unqualified post-1.0 claim ({', '.join(hits)}): "
                + prose.strip()[:100]
            )

for p in problems:
    print(f"check-docs: {p}")
print(f"check-docs: {checked} document(s) checked, {len(problems)} violation(s)")
sys.exit(0 if not problems else 1)
PY
