#!/bin/sh
# check-ci-gate.sh - Phase 2 exit gate, extended with the repository CI facts.
#
# It requires five facts about the current HEAD commit:
#   1. a check run named "kernel-compat" concluded success on that exact commit;
#   2. "kernel-compat" is a required status check on the branch in branch protection;
#   3. .github/workflows/ci.yml exists and declares the four required jobs;
#   4. the newest completed push, pull_request or workflow_dispatch run of
#      .github/workflows/ci.yml on HEAD concluded success. Scheduled (cron)
#      runs are excluded: the weekly coverage job runs on the same workflow,
#      so a green schedule run must not hide a failed push run on HEAD;
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
#
# --self-test also exercises the run selection over canned GitHub Actions JSON
# (schedule-only, a schedule run shadowing a failed push run, a newer failed
# pull_request run, a wrong-workflow run, a wrong-commit run and an in-progress
# run). It needs python3, but no token and no network.

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

# ci_run_select FILE SHA WORKFLOW picks the newest workflow run on SHA that a
# merge or a manual dispatch actually triggered. Scheduled (cron) runs are
# excluded on purpose: ci.yml also runs weekly for the coverage job, and a
# green schedule run must not shadow a failed push run on the same commit.
# It prints one machine-readable line
#   event=<event> run_number=<n> status=<status> conclusion=<conclusion>
# and, when the winner is not a completed success, the human line
#   ci-gate: latest <workflow> on <sha> is <status>/<conclusion>
# on stderr. Exit 0 only when the winner concluded success.
ci_run_select () {
	python3 - "$1" "$2" "$3" <<'PY'
import json, sys

path, sha, workflow = sys.argv[1], sys.argv[2], sys.argv[3]
with open(path) as fh:
    data = json.load(fh)
qualifying = {"push", "pull_request", "workflow_dispatch"}
runs = [r for r in data.get("workflow_runs", [])
        if r.get("event") in qualifying
        and (r.get("path") or "").endswith(workflow)
        and r.get("head_sha") == sha]
if not runs:
    print("ci-gate: no push/pull_request/workflow_dispatch run of %s on %s"
          % (workflow, sha), file=sys.stderr)
    sys.exit(1)
best = max(runs, key=lambda r: (r.get("created_at") or r.get("run_started_at") or "",
                                r.get("run_number") or 0,
                                r.get("id") or 0))
print("event=%s run_number=%s status=%s conclusion=%s" % (
    best.get("event"), best.get("run_number"), best.get("status"),
    best.get("conclusion")))
if best.get("status") != "completed" or best.get("conclusion") != "success":
    print("ci-gate: latest %s on %s is %s/%s" % (
        workflow, sha, best.get("status"), best.get("conclusion")),
        file=sys.stderr)
    sys.exit(1)
PY
}

selftest_runs () {
	command -v python3 >/dev/null 2>&1 || { echo "ci-gate: python3 not found" >&2; return 1; }
	st=$(mktemp -d) || { echo "ci-gate: self-test: cannot create a temporary directory" >&2; return 1; }
	trap 'rm -rf "$st"' EXIT HUP INT TERM
	st_sha=$(git rev-parse HEAD 2>/dev/null) || st_sha=0000000000000000000000000000000000000000
	ok=1
	run_case () {
		rc_name=$1 rc_want=$2 rc_line=$3
		printf '%s\n' "$4" > "$st/$rc_name.json"
		rc_out=$(ci_run_select "$st/$rc_name.json" "$st_sha" "$CI_WORKFLOW" 2>"$st/$rc_name.err")
		rc_got=$?
		if [ "$rc_got" -ne "$rc_want" ]; then
			echo "ci-gate: self-test[$rc_name]: exit $rc_got, want $rc_want" >&2
			ok=0
		fi
		if [ "$rc_out" != "$rc_line" ]; then
			echo "ci-gate: self-test[$rc_name]: selection '$rc_out', want '$rc_line'" >&2
			ok=0
		fi
		if [ -n "${5:-}" ] && ! grep -Fq "$5" "$st/$rc_name.err"; then
			echo "ci-gate: self-test[$rc_name]: stderr misses '$5'" >&2
			ok=0
		fi
	}
	run_case schedule-only 1 '' "{\"workflow_runs\":[{\"event\":\"schedule\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":11,\"created_at\":\"2026-10-26T05:00:00Z\",\"run_started_at\":\"2026-10-26T05:00:00Z\",\"id\":11}]}" 'no push/pull_request/workflow_dispatch run'
	run_case schedule-shadows-red-push 1 'event=push run_number=10 status=completed conclusion=failure' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"failure\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10},{\"event\":\"schedule\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":11,\"created_at\":\"2026-10-26T05:00:00Z\",\"run_started_at\":\"2026-10-26T05:00:00Z\",\"id\":11}]}" 'ci-gate: latest .github/workflows/ci.yml on '
	run_case schedule-plus-green-push 0 'event=push run_number=10 status=completed conclusion=success' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10},{\"event\":\"schedule\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":11,\"created_at\":\"2026-10-26T05:00:00Z\",\"run_started_at\":\"2026-10-26T05:00:00Z\",\"id\":11}]}"
	run_case pull-request-newer-red 1 'event=pull_request run_number=12 status=completed conclusion=failure' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10},{\"event\":\"pull_request\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"failure\",\"run_number\":12,\"created_at\":\"2026-10-26T09:00:00Z\",\"run_started_at\":\"2026-10-26T09:00:00Z\",\"id\":12}]}"
	run_case workflow-dispatch-only 0 'event=workflow_dispatch run_number=3 status=completed conclusion=success' "{\"workflow_runs\":[{\"event\":\"workflow_dispatch\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":3,\"created_at\":\"2026-10-25T12:00:00Z\",\"run_started_at\":\"2026-10-25T12:00:00Z\",\"id\":3}]}"
	run_case other-workflow-ignored 0 'event=push run_number=10 status=completed conclusion=success' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10},{\"event\":\"push\",\"path\":\".github/workflows/other.yml\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"failure\",\"run_number\":12,\"created_at\":\"2026-10-26T09:00:00Z\",\"run_started_at\":\"2026-10-26T09:00:00Z\",\"id\":12}]}"
	run_case wrong-commit 1 '' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"1111111111111111111111111111111111111111\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10}]}" 'no push/pull_request/workflow_dispatch run'
	run_case in-progress-newest 1 'event=push run_number=13 status=in_progress conclusion=None' "{\"workflow_runs\":[{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"completed\",\"conclusion\":\"success\",\"run_number\":10,\"created_at\":\"2026-10-20T10:00:00Z\",\"run_started_at\":\"2026-10-20T10:00:00Z\",\"id\":10},{\"event\":\"push\",\"path\":\"$CI_WORKFLOW\",\"head_sha\":\"$st_sha\",\"status\":\"in_progress\",\"conclusion\":null,\"run_number\":13,\"created_at\":\"2026-10-26T11:00:00Z\",\"run_started_at\":\"2026-10-26T11:00:00Z\",\"id\":13}]}"
	return $((1 - ok))
}

if [ "${1:-}" = --self-test ]; then
	check_ci_workflow || exit 1
	selftest_runs || exit 1
	echo "ci-gate: self-test: OK (8 canned run-selection cases; $CI_WORKFLOW declares: $CI_JOBS)"
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

ci_run_select "$tmp/workflows.json" "$sha" "$CI_WORKFLOW" >/dev/null || exit 1

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
