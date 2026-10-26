#!/bin/sh
# check-ci-gate.sh - Phase 2 exit gate, extended with the repository CI facts.
#
# It requires five facts about the current HEAD commit:
#   1. a check run named "kernel-compat" concluded success on that exact commit;
#   2. "kernel-compat" is a required status check on the branch in branch protection;
#   3. .github/workflows/ci.yml exists and declares the four required jobs;
#   4. a completed run of .github/workflows/ci.yml concluded success on HEAD;
#   5. the CI gate context (default "gate-self-test") is required by branch
#      protection, so a red CI gate blocks a merge instead of only warning.
#
# Fact 1 and fact 2 keep their original output line unchanged:
#   ci-gate: kernel-compat success=<sha> required=true
# The CI facts add one line after it. When no token is available the script
# exits 2 (data unavailable), never 0, and prints no success line.
#
# Usage: scripts/check-ci-gate.sh [--self-test]
# Environment:
#   GH_TOKEN or GITHUB_TOKEN  token with repo read access (required)
#   CRUG_REPO                 owner/repo override (default: parsed from origin)
#   CRUG_BRANCH               branch to inspect (default: main)
#   CI_REQUIRED_CONTEXT       CI job required by branch protection
#                             (default: gate-self-test)
# Exit: 0 all facts hold; 1 a fact is missing or false; 2 tooling/token missing.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT" || exit 2
CI_WORKFLOW=.github/workflows/ci.yml
CI_JOBS='build compiler runtime gate-self-test'

case ${1:-} in
	'') ;;
	--self-test) ;;
	-h|--help) sed -n '2,32p' "$0"; exit 0 ;;
	*) echo "ci-gate: unknown argument $1" >&2; exit 2 ;;
esac

# ci_jobs FILE prints the job ids declared directly under top-level jobs:.
ci_jobs () {
	awk '
		/^jobs:[[:space:]]*$/ { on = 1; next }
		on && /^[^[:space:]]/ { on = 0 }
		on && /^  [A-Za-z0-9_-]+:[[:space:]]*$/ {
			line = $0
			sub(/^  /, "", line)
			sub(/:.*/, "", line)
			print line
		}' "$1"
}

check_ci_workflow () {
	[ -f "$CI_WORKFLOW" ] || { echo "ci-gate: missing $CI_WORKFLOW" >&2; return 1; }
	jobs=$(ci_jobs "$CI_WORKFLOW")
	for job in $CI_JOBS; do
		printf '%s\n' "$jobs" | grep -qx "$job" || {
			echo "ci-gate: $CI_WORKFLOW does not declare job '$job'" >&2
			return 1
		}
	done
	grep -q '^on:[[:space:]]*$' "$CI_WORKFLOW" || { echo "ci-gate: $CI_WORKFLOW has no on: trigger" >&2; return 1; }
	grep -q '^  workflow_dispatch:' "$CI_WORKFLOW" || { echo "ci-gate: $CI_WORKFLOW is not manually runnable" >&2; return 1; }
	return 0
}

if [ "${1:-}" = --self-test ]; then
	check_ci_workflow || exit 1
	echo "ci-gate: self-test: OK ($CI_WORKFLOW declares: $CI_JOBS)"
	exit 0
fi

command -v python3 >/dev/null 2>&1 || { echo "ci-gate: python3 not found" >&2; exit 2; }
command -v curl >/dev/null 2>&1 || { echo "ci-gate: curl not found" >&2; exit 2; }

token=${GH_TOKEN:-${GITHUB_TOKEN:-}}
if [ -z "$token" ]; then
	echo "ci-gate: no token. Set GH_TOKEN or GITHUB_TOKEN to a token with repo read access." >&2
	echo "ci-gate: without a token the CI facts are unavailable, so this gate cannot pass." >&2
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
ci_context=${CI_REQUIRED_CONTEXT:-gate-self-test}
sha=$(git rev-parse HEAD) || exit 2

check_ci_workflow || exit 1

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
best = max(runs, key=lambda r: (r.get("started_at") or "", r.get("id") or 0))
if best.get("status") != "completed" or best.get("conclusion") != "success":
    print("ci-gate: kernel-compat on %s is %s/%s" % (
        sha, best.get("status"), best.get("conclusion")), file=sys.stderr)
    sys.exit(1)
PY
runs_rc=$?
[ "$runs_rc" -eq 0 ] || exit 1

code=$(curl -sS -o "$tmp/workflows.json" -w '%{http_code}' \
	-H "$auth" -H "$accept" \
	"$api/repos/$repo/actions/runs?head_sha=$sha&per_page=100") || {
	echo "ci-gate: cannot reach the GitHub Actions runs endpoint" >&2
	exit 1
}
case $code in
200) ;;
401|403|404)
	echo "ci-gate: Actions runs request for $repo@$sha returned HTTP $code." >&2
	echo "ci-gate: the token needs Actions read access and this commit must have a CI run." >&2
	exit 1
	;;
*)
	echo "ci-gate: unexpected HTTP $code from the Actions runs endpoint" >&2
	exit 1
	;;
esac

python3 - "$tmp/workflows.json" "$sha" "$CI_WORKFLOW" <<'PY'
import json, sys
path, sha, workflow = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as fh:
    data = json.load(fh)
runs = [r for r in data.get("workflow_runs", [])
        if (r.get("path") or "").endswith(workflow)
        and r.get("head_sha") == sha]
if not runs:
    print("ci-gate: no run of %s on %s" % (workflow, sha), file=sys.stderr)
    sys.exit(1)
best = max(runs, key=lambda r: (r.get("run_started_at") or "",
                                r.get("run_number") or 0))
if best.get("status") != "completed" or best.get("conclusion") != "success":
    print("ci-gate: latest %s on %s is %s/%s" % (
        workflow, sha, best.get("status"), best.get("conclusion")),
        file=sys.stderr)
    sys.exit(1)
PY
wf_rc=$?
[ "$wf_rc" -eq 0 ] || exit 1

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
	echo "ci-gate: enable it and require the kernel-compat and $ci_context checks." >&2
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

python3 - "$tmp/protection.json" "kernel-compat" <<'PY'
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

python3 - "$tmp/protection.json" "$ci_context" <<'PY'
import json, sys
with open(sys.argv[1]) as fh:
    data = json.load(fh)
wanted = sys.argv[2]
rsc = data.get("required_status_checks") or {}
contexts = list(rsc.get("contexts") or [])
checks = [c.get("context") for c in (rsc.get("checks") or [])]
if wanted not in contexts and wanted not in checks:
    print("ci-gate: %s is not a required status check" % wanted, file=sys.stderr)
    print("ci-gate: required now: %s" % (", ".join(contexts + [c for c in checks if c]) or "(none)"),
          file=sys.stderr)
    sys.exit(1)
PY
ctx_rc=$?
[ "$ctx_rc" -eq 0 ] || exit 1

echo "ci-gate: kernel-compat success=$sha required=true"
echo "ci-gate: ci workflow=$CI_WORKFLOW success=$sha context=protected"
exit 0
