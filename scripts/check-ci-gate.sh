#!/bin/sh
# check-ci-gate.sh - Phase 2 exit gate.
#
# Requires two facts from GitHub about the current HEAD commit:
#   1. a check run named "kernel-compat" concluded success on that exact commit;
#   2. "kernel-compat" is a required status check on the branch in branch protection.
# Both are owner-side configuration; this script only observes them.
#
# Usage: scripts/check-ci-gate.sh
# Environment:
#   GH_TOKEN or GITHUB_TOKEN  token with repo read access (required)
#   CRUG_REPO                 owner/repo override (default: parsed from origin)
#   CRUG_BRANCH               branch to inspect (default: main)
# Exit: 0 both facts hold; 1 a fact is missing or false; 2 tooling/token missing.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2

command -v python3 >/dev/null 2>&1 || { echo "ci-gate: python3 not found" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "ci-gate: curl not found" >&2; exit 2; }

token=${GH_TOKEN:-${GITHUB_TOKEN:-}}
if [ -z "$token" ]; then
	echo "ci-gate: no token. Set GH_TOKEN or GITHUB_TOKEN to a token with repo read access." >&2
	exit 2
fi

repo=${CRUG_REPO:-}
if [ -z "$repo" ]; then
	repo=$(git remote get-url origin 2>/dev/null | sed -E 's#^.*github\.com[:/]##; s#\.git$##')
fi
if [ -z "$repo" ]; then
	echo "ci-gate: cannot determine owner/repo; set CRUG_REPO=owner/repo" >&2
	exit 2
fi

branch=${CRUG_BRANCH:-main}
sha=$(git rev-parse HEAD) || exit 2
tmp=$(mktemp -d) || exit 2
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

api=https://api.github.com
auth="Authorization: Bearer $token"
accept="Accept: application/vnd.github+json"

code=$(curl -sS -o "$tmp/runs.json" -w '%{http_code}' \
	-H "$auth" -H "$accept" \
	"$api/repos/$repo/commits/$sha/check-runs?per_page=100") || {
	echo "ci-gate: cannot reach the GitHub API" >&2
	exit 1
}
case $code in
200) ;;
401|403|404)
	echo "ci-gate: check-runs request for $repo@$sha returned HTTP $code." >&2
	echo "ci-gate: the repository must be visible to the token and Actions must have run." >&2
	exit 1
	;;
*)
	echo "ci-gate: unexpected HTTP $code from the check-runs endpoint" >&2
	exit 1
	;;
esac

python3 - "$tmp/runs.json" "$sha" <<'PY'
import json, sys
path, sha = sys.argv[1], sys.argv[2]
with open(path) as fh:
    data = json.load(fh)
runs = [r for r in data.get("check_runs", [])
        if r.get("name") == "kernel-compat" and r.get("head_sha") == sha]
if not runs:
    print("ci-gate: no kernel-compat check run on %s" % sha, file=sys.stderr)
    sys.exit(1)
best = max(runs, key=lambda r: r.get("completed_at") or "")
if best.get("status") != "completed" or best.get("conclusion") != "success":
    print("ci-gate: kernel-compat on %s is %s/%s" % (
        sha, best.get("status"), best.get("conclusion")), file=sys.stderr)
    sys.exit(1)
PY
runs_rc=$?
[ "$runs_rc" -eq 0 ] || exit 1

code=$(curl -sS -o "$tmp/protection.json" -w '%{http_code}' \
	-H "$auth" -H "$accept" \
	"$api/repos/$repo/branches/$branch/protection") || {
	echo "ci-gate: cannot reach the branch-protection endpoint" >&2
	exit 1
}
case $code in
200) ;;
404)
	echo "ci-gate: branch protection is not enabled for $branch." >&2
	echo "ci-gate: enable it and require the kernel-compat check (docs/crug/KERNEL-COMPATIBILITY.md)." >&2
	exit 1
	;;
401|403)
	echo "ci-gate: branch-protection request returned HTTP $code; the token needs admin access to the repository." >&2
	exit 1
	;;
*)
	echo "ci-gate: unexpected HTTP $code from the branch-protection endpoint" >&2
	exit 1
	;;
esac

python3 - "$tmp/protection.json" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    data = json.load(fh)
rsc = data.get("required_status_checks") or {}
contexts = list(rsc.get("contexts") or [])
checks = [c.get("context") for c in (rsc.get("checks") or [])]
if "kernel-compat" not in contexts and "kernel-compat" not in checks:
    print("ci-gate: kernel-compat is not a required status check", file=sys.stderr)
    print("ci-gate: required now: %s" % (", ".join(contexts + [c for c in checks if c]) or "(none)"),
          file=sys.stderr)
    sys.exit(1)
PY
req_rc=$?
[ "$req_rc" -eq 0 ] || exit 1

echo "ci-gate: kernel-compat success=$sha required=true"
exit 0
