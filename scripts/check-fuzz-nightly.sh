#!/bin/sh
# check-fuzz-nightly.sh - prove that the scheduled fuzz-nightly workflow is green.
#
# .github/workflows/fuzz-nightly.yml runs the ownership, borrow and channel fuzz
# target plus the ASan/UBSan suite once a day (cron '17 3 * * *'). This gate
# counts the consecutive scheduled successes that end at the newest scheduled
# run and compares the count with --window. A newer push run does not break the
# window; a newer failed, cancelled or in-progress scheduled run does.
#
# Data sources, in order:
#
#   1. the anonymous GitHub REST API
#      https://api.github.com/repos/<repo>/actions/workflows/fuzz-nightly.yml/runs?per_page=100
#      whose JSON carries run_number, event, status, conclusion and created_at
#      from the run record itself. Anonymous callers get 60 requests per hour,
#      so an HTTP 403 rate-limit answer is expected sometimes;
#   2. the public workflow HTML page
#      https://github.com/<repo>/actions/workflows/fuzz-nightly.yml
#      whose '<script type="application/json" data-target="react-app.embeddedData">'
#      block carries payload.workflowRunsRoute.workflowRuns. That payload has no
#      'event' field (verified against the live kernel-compat.yml page), so the
#      fallback asks the server to filter with '?query=event%3Aschedule' and
#      reads runNumber, state.conclusion (enum values such as
#      WORKFLOW_RUN_CONCLUSION_SUCCESS) and createdAt.timestamp from the
#      filtered runs.
#
# A run counts only when its event is 'schedule' and its conclusion is success.
# The window is the run of such successes immediately before the newest
# scheduled run: counting stops at the first scheduled run that is not a
# success.
#
# Usage:
#   scripts/check-fuzz-nightly.sh                     window 30 on cleg0078/crug
#   scripts/check-fuzz-nightly.sh --window 7
#   scripts/check-fuzz-nightly.sh --repo OWNER/NAME --json
#   scripts/check-fuzz-nightly.sh --workflow kernel-compat.yml --source html
#
# --source auto (default) tries the API and falls back to the HTML page;
# --source api and --source html force one source. --workflow and --source exist
# so the gate itself can be verified against another workflow; the fuzz-nightly
# gate runs with the defaults.
#
# Exit status: 0 the consecutive scheduled successes reach the window,
# 1 the window is not met, 2 the history cannot be fetched or parsed (rate
# limit, no runs yet, a workflow page that does not exist) or a usage error.
# An unreadable source is exit 2, never exit 0: the script does not invent runs.
set -u

NAME=check-fuzz-nightly
WORKFLOW=fuzz-nightly.yml
repo=cleg0078/crug
window=30
source=auto
json=0

say () { printf '%s: %s\n' "$NAME" "$*"; }
warn () { printf '%s: %s\n' "$NAME" "$*" >&2; }
die () { warn "$*"; exit 2; }
usage () { sed -n '2,/^set -u$/p' "$0" | sed '$d'; }

while [ $# -gt 0 ]; do
	case $1 in
	--window)
		[ $# -ge 2 ] || die "--window needs a value"
		case $2 in ''|*[!0-9]*) die "--window expects a whole number, got '$2'" ;; esac
		window=$2
		shift 2
		;;
	--repo)
		[ $# -ge 2 ] || die "--repo needs OWNER/NAME"
		repo=$2
		case $repo in
		*/*/*|/*|*/|*' '*) die "--repo expects OWNER/NAME, got '$repo'" ;;
		*/*) ;;
		*) die "--repo expects OWNER/NAME, got '$repo'" ;;
		esac
		shift 2
		;;
	--workflow)
		[ $# -ge 2 ] || die "--workflow needs a file name"
		WORKFLOW=$2
		case $WORKFLOW in
		''|*/*|*' '*) die "--workflow expects a file name, got '$WORKFLOW'" ;;
		esac
		shift 2
		;;
	--source)
		[ $# -ge 2 ] || die "--source needs auto, api or html"
		source=$2
		case $source in
		auto|api|html) ;;
		*) die "--source expects auto, api or html, got '$source'" ;;
		esac
		shift 2
		;;
	--json) json=1; shift ;;
	-h|--help) usage; exit 0 ;;
	*) die "unknown argument $1" ;;
	esac
done

[ "$window" -ge 1 ] || die "--window must be at least 1"

TMP=$(mktemp -d) || die "cannot create a temporary directory"
trap 'rm -rf "$TMP"' 0 1 2 15

# The helper reads one data source. 'api' parses the file the shell downloaded;
# 'html' fetches the filtered workflow pages with the Python standard library.
# Exit status 0 and 1 are the gate result, 2 means the history is unreadable
# (for example no scheduled run yet), 3 means this source failed and the other
# one may still work.
helper () {
	python3 - "$@" <<'PY_EOF'
import json
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

NAME = "check-fuzz-nightly"
PREFIX = "fuzz-nightly"


def main():
    mode = sys.argv[1]
    datafile = sys.argv[2]
    http_code = sys.argv[3] or "000"
    window = int(sys.argv[4])
    repo = sys.argv[5]
    workflow = sys.argv[6]
    as_json = sys.argv[7] == "1"
    note_file = sys.argv[8] if len(sys.argv) > 8 else ""

    def api_note():
        if not note_file:
            return None
        try:
            with open(note_file, "r", encoding="utf-8", errors="replace") as handle:
                text = handle.read().strip()
        except OSError:
            return None
        if not text:
            return None
        first = text.splitlines()[0]
        if first.startswith(NAME + ": "):
            first = first[len(NAME) + 2:]
        return first

    def emit(ok, count, earliest, latest, source):
        if as_json:
            report = {
                "repo": repo,
                "workflow": workflow,
                "window": window,
                "consecutive": count,
                "earliest": earliest,
                "latest": latest,
                "source": source,
                "ok": ok,
            }
            api_error = api_note()
            if api_error:
                report["api_error"] = api_error
            sys.stdout.write(json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n")
        else:
            sys.stdout.write(
                "%s: %d consecutive scheduled success(es), window %d, "
                "earliest %s, latest %s\n"
                % (PREFIX, count, window, earliest or "none", latest or "none")
            )
        sys.exit(0 if ok else 1)

    def unavailable(reason, exit_code, source):
        if as_json:
            report = {
                "repo": repo,
                "workflow": workflow,
                "window": window,
                "consecutive": None,
                "earliest": None,
                "latest": None,
                "source": source,
                "ok": False,
                "reason": reason,
            }
            api_error = api_note()
            if api_error:
                report["api_error"] = api_error
            sys.stdout.write(json.dumps(report, sort_keys=True, separators=(",", ":")) + "\n")
        sys.stderr.write("%s: %s\n" % (NAME, reason))
        sys.exit(exit_code)

    def window_count(ordered):
        """Count scheduled successes from the newest run; ordered is newest first."""
        count = 0
        earliest = None
        latest = None
        for success, created in ordered:
            if not success:
                break
            if count == 0:
                latest = created
            earliest = created
            count += 1
        return count, earliest, latest

    if mode == "api":
        try:
            with open(datafile, "r", encoding="utf-8", errors="replace") as handle:
                payload = json.load(handle)
        except (OSError, ValueError) as error:
            unavailable("api: unreadable response (%s)" % error, 3, "api")
        if not isinstance(payload, dict):
            unavailable("api: unexpected JSON payload", 3, "api")
        if "workflow_runs" not in payload:
            message = payload.get("message") or "no workflow_runs field"
            unavailable("api: HTTP %s: %s" % (http_code, message), 3, "api")

        runs = [run for run in payload.get("workflow_runs") or [] if isinstance(run, dict)]
        if not runs:
            unavailable("api: the workflow has no runs yet", 2, "api")
        runs.sort(key=lambda run: run.get("created_at") or "", reverse=True)
        ordered = [
            (run.get("conclusion") == "success", run.get("created_at"))
            for run in runs
            if run.get("event") == "schedule"
        ]
        if not ordered:
            unavailable("api: the workflow has no scheduled runs yet", 2, "api")

        count, earliest, latest = window_count(ordered)
        total = payload.get("total_count")
        truncated = isinstance(total, int) and total > len(runs)
        if count < window and count == len(ordered) and truncated:
            unavailable(
                "api: only the newest %d runs were read and every scheduled one "
                "succeeded; more history is needed for window %d" % (len(runs), window),
                2,
                "api",
            )
        emit(count >= window, count, earliest, latest, "api")

    # mode == "html": the workflow page filtered to scheduled runs. The
    # react-app.embeddedData payload has no 'event' field, so the query filter
    # is what identifies scheduled runs; runNumber, state.conclusion and
    # createdAt come from the payload itself.
    base = "https://github.com/%s/actions/workflows/%s" % (repo, workflow)
    filtered = "?query=" + urllib.parse.quote("event:schedule", safe="")

    def fetch(page):
        url = base + filtered + ("&page=%d" % page if page > 1 else "")
        request = urllib.request.Request(
            url, headers={"User-Agent": "check-fuzz-nightly/1 (crug gate)"}
        )
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                return response.status, response.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode("utf-8", "replace")
        except (urllib.error.URLError, OSError) as error:
            return None, str(error)

    def parse_page(body):
        match = re.search(
            r'<script type="application/json" data-target="react-app\.embeddedData">'
            r"(.*?)</script>",
            body,
            re.S,
        )
        if not match:
            return None, "the workflow page has no react-app.embeddedData payload"
        try:
            document = json.loads(match.group(1))
        except ValueError as error:
            return None, "the embedded payload is not JSON (%s)" % error
        payload = document.get("payload") if isinstance(document, dict) else None
        route = payload.get("workflowRunsRoute") if isinstance(payload, dict) else None
        if not isinstance(route, dict):
            return None, "the embedded payload has no payload.workflowRunsRoute"
        return route, None

    ordered = []
    seen = set()
    page = 1
    total_pages = 1

    while True:
        status, body = fetch(page)
        if status != 200:
            unavailable("html: HTTP %s while fetching the workflow page" % status, 3, "html")
        route, problem = parse_page(body)
        if problem:
            unavailable("html: " + problem, 3, "html")
        if route.get("workflowExists") is False:
            unavailable(
                "html: workflow %s does not exist in %s" % (workflow, repo), 3, "html"
            )

        total_pages = route.get("totalPages") or 1
        runs = route.get("workflowRuns")
        if runs is None:
            if page == 1:
                unavailable("html: the workflow has no scheduled runs yet", 2, "html")
            break

        for run in runs:
            if not isinstance(run, dict):
                continue
            number = run.get("runNumber")
            if number in seen:
                continue
            seen.add(number)
            run_state = run.get("state") if isinstance(run.get("state"), dict) else {}
            created = run.get("createdAt")
            if isinstance(created, dict):
                created = created.get("timestamp")
            ordered.append(
                (run_state.get("conclusion") == "WORKFLOW_RUN_CONCLUSION_SUCCESS", created)
            )
        ordered.sort(key=lambda item: item[1] or "", reverse=True)

        count, _, _ = window_count(ordered)
        if count >= window or count < len(ordered):
            break
        if page >= total_pages or page >= 10:
            break
        page += 1

    count, earliest, latest = window_count(ordered)
    if count < window and count == len(ordered) and page < total_pages:
        unavailable(
            "html: only the newest %d scheduled runs were read; more history is "
            "needed for window %d" % (len(ordered), window),
            2,
            "html",
        )
    emit(count >= window, count, earliest, latest, "html")


main()

PY_EOF
}

api_url="https://api.github.com/repos/$repo/actions/workflows/$WORKFLOW/runs?per_page=100"

if [ "$source" = html ]; then
	helper html "" "" "$window" "$repo" "$WORKFLOW" "$json" "" >"$TMP/html.out" 2>"$TMP/html.err"
	status=$?
	cat "$TMP/html.out"
	cat "$TMP/html.err" >&2
	[ "$status" -eq 3 ] && exit 2
	exit "$status"
fi

http=$(curl -sS -L --max-time 30 -o "$TMP/api.json" -w '%{http_code}' "$api_url" 2>"$TMP/curl.err") || http=000
helper api "$TMP/api.json" "$http" "$window" "$repo" "$WORKFLOW" "$json" "" >"$TMP/api.out" 2>"$TMP/api.err"
status=$?

case $status in
0|1)
	# A gate result: the API answered and the run history was readable.
	cat "$TMP/api.out"
	exit "$status"
	;;
2)
	# The API answered but the history is not usable (no runs yet, no
	# scheduled runs yet, more history needed than one page holds): report
	# why instead of pretending the window failed.
	cat "$TMP/api.out"
	cat "$TMP/api.err" >&2
	exit 2
	;;
esac

if [ "$source" = api ]; then
	cat "$TMP/api.err" >&2
	cat "$TMP/api.out"
	exit 2
fi

warn "the anonymous GitHub API was unusable; falling back to the workflow HTML page"
cat "$TMP/api.err" >&2

helper html "" "" "$window" "$repo" "$WORKFLOW" "$json" "$TMP/api.err" >"$TMP/html.out" 2>"$TMP/html.err"
status=$?
cat "$TMP/html.out"
cat "$TMP/html.err" >&2
[ "$status" -eq 3 ] && exit 2
exit "$status"
