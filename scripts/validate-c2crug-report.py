#!/usr/bin/env python3
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
"""Validate a c2crug JSON report against the documented schema.

This is an independent checker: it does not import the c2crug tool, so a
bug in the tool cannot make its own report look valid.  It checks the
structural contract plus the safety rules of ADR 0023: only a proven own
entry may be applied, and every applied entry carries evidence.
"""

import argparse
import json
import re
import sys

SCHEMA_ID = "crug-c2crug-report-v1"
HEX64 = re.compile(r"^[0-9a-f]{64}$")

TOP_KEYS = {"schema", "tool", "version", "mode", "sources", "entries",
            "counts"}
ENTRY_KEYS = {"file", "line", "column", "kind", "applied", "confidence",
              "reason", "evidence", "suggestion", "transformation",
              "preprocessed", "eligibility"}
EVIDENCE_KEYS = {"file", "line", "text"}
TRANSFORM_KEYS = {"declaration_line", "release_line", "release_from",
                  "release_to"}
COUNTS_KEYS = {"own", "raw", "borrow_suggestion", "applied"}


def is_int(value):
    return isinstance(value, int) and not isinstance(value, bool)


def check(report):
    errors = []
    if not isinstance(report, dict):
        return ["report is not an object"]
    missing = TOP_KEYS - set(report)
    extra = set(report) - TOP_KEYS
    if missing:
        errors.append("missing top-level key(s): %s" % ", ".join(sorted(missing)))
    if extra:
        errors.append("unknown top-level key(s): %s" % ", ".join(sorted(extra)))
    if report.get("schema") != SCHEMA_ID:
        errors.append("schema is not %s" % SCHEMA_ID)
    if report.get("tool") != "c2crug":
        errors.append("tool is not c2crug")
    if not isinstance(report.get("version"), str) or \
            not re.match(r"^[0-9]+\.[0-9]+\.[0-9]+$",
                         str(report.get("version", ""))):
        errors.append("version is not a semantic version")
    if report.get("mode") not in ("analyze", "safe"):
        errors.append("mode is not analyze or safe")
    sources = report.get("sources")
    if not isinstance(sources, list) or not sources:
        errors.append("sources must be a non-empty array")
        sources = []
    for src in sources:
        if not isinstance(src, dict) or set(src) != {"file", "sha256"}:
            errors.append("source entry must have file and sha256")
            continue
        if not isinstance(src["file"], str) or not src["file"]:
            errors.append("source file must be a non-empty string")
        if not isinstance(src["sha256"], str) or not HEX64.match(src["sha256"]):
            errors.append("source sha256 is not 64 lowercase hex digits")
    entries = report.get("entries")
    if not isinstance(entries, list):
        errors.append("entries must be an array")
        entries = []
    own = raw = suggestion = applied = 0
    for index, entry in enumerate(entries):
        where = "entries[%d]" % index
        if not isinstance(entry, dict):
            errors.append("%s is not an object" % where)
            continue
        missing = {"file", "line", "column", "kind", "applied", "confidence",
                   "reason", "evidence", "preprocessed",
                   "eligibility"} - set(entry)
        if missing:
            errors.append("%s missing %s" % (where, ", ".join(sorted(missing))))
            continue
        if set(entry) - ENTRY_KEYS:
            errors.append("%s has unknown key(s) %s"
                          % (where, ", ".join(sorted(set(entry) - ENTRY_KEYS))))
        if not isinstance(entry["file"], str) or not entry["file"]:
            errors.append("%s file is empty" % where)
        for key in ("line", "column"):
            if not is_int(entry[key]) or entry[key] < 1:
                errors.append("%s %s is not a positive integer" % (where, key))
        if not isinstance(entry["reason"], str) or not entry["reason"]:
            errors.append("%s reason is empty" % where)
        if not isinstance(entry["applied"], bool):
            errors.append("%s applied is not a boolean" % where)
        if not isinstance(entry["preprocessed"], bool):
            errors.append("%s preprocessed is not a boolean" % where)
        if entry["eligibility"] not in ("eligible", "ineligible"):
            errors.append("%s eligibility is not eligible or ineligible"
                          % where)
        elif entry["eligibility"] == "ineligible":
            if entry["applied"]:
                errors.append("%s an ineligible entry may not be applied"
                              % where)
            if entry["confidence"] != 0:
                errors.append("%s an ineligible entry needs confidence 0"
                              % where)
        if not isinstance(entry["confidence"], (int, float)) or \
                isinstance(entry["confidence"], bool) or \
                not 0 <= entry["confidence"] <= 1:
            errors.append("%s confidence is not in [0, 1]" % where)
        ev = entry.get("evidence")
        if not isinstance(ev, dict) or set(ev) != EVIDENCE_KEYS:
            errors.append("%s evidence must have exactly file, line, text"
                          % where)
        else:
            if not isinstance(ev["file"], str) or not ev["file"]:
                errors.append("%s evidence file is empty" % where)
            if not is_int(ev["line"]) or ev["line"] < 1:
                errors.append("%s evidence line is not positive" % where)
            if not isinstance(ev["text"], str) or not ev["text"]:
                errors.append("%s evidence text is empty" % where)
        kind = entry["kind"]
        if kind == "own":
            own += 1
        elif kind == "raw":
            raw += 1
        elif kind == "borrow-suggestion":
            suggestion += 1
            if entry.get("suggestion") not in ("ref", "mut"):
                errors.append("%s borrow-suggestion needs ref or mut" % where)
            if entry["applied"]:
                errors.append("%s a suggestion is never applied" % where)
        else:
            errors.append("%s unknown kind %r" % (where, kind))
        if entry["applied"]:
            applied += 1
            if kind != "own":
                errors.append("%s only an own entry may be applied" % where)
            if not entry["confidence"] > 0:
                errors.append("%s applied entry needs a non-zero confidence"
                              % where)
            tr = entry.get("transformation")
            if not isinstance(tr, dict) or set(tr) != TRANSFORM_KEYS:
                errors.append("%s applied entry needs a transformation" % where)
            else:
                for key in ("declaration_line", "release_line"):
                    if not is_int(tr[key]) or tr[key] < 1:
                        errors.append("%s transformation %s is not positive"
                                      % (where, key))
                if tr.get("release_to") != "drop":
                    errors.append("%s transformation release_to is not drop"
                                  % where)
    counts = report.get("counts")
    if not isinstance(counts, dict) or set(counts) != COUNTS_KEYS:
        errors.append("counts must have exactly %s"
                      % ", ".join(sorted(COUNTS_KEYS)))
    else:
        for key in COUNTS_KEYS:
            if not is_int(counts[key]) or counts[key] < 0:
                errors.append("counts.%s is not a non-negative integer" % key)
        if counts.get("own") != own:
            errors.append("counts.own does not match the own entries")
        if counts.get("raw") != raw:
            errors.append("counts.raw does not match the raw entries")
        if counts.get("borrow_suggestion") != suggestion:
            errors.append("counts.borrow_suggestion does not match")
        if counts.get("applied") != applied:
            errors.append("counts.applied does not match")
        if report.get("mode") == "analyze" and applied:
            errors.append("analyze mode must not apply any entry")
    return errors


def main(argv):
    ap = argparse.ArgumentParser(description="validate a c2crug report")
    ap.add_argument("report")
    args = ap.parse_args(argv)
    try:
        with open(args.report, "r", encoding="utf-8") as f:
            report = json.load(f)
    except (OSError, ValueError) as exc:
        print("c2crug-report: cannot read %s: %s" % (args.report, exc),
              file=sys.stderr)
        return 2
    errors = check(report)
    if errors:
        for err in errors:
            print("c2crug-report: invalid: %s" % err, file=sys.stderr)
        return 1
    counts = report["counts"]
    print("c2crug-report: valid (%d own, %d raw, %d suggestion(s), %d applied)"
          % (counts["own"], counts["raw"], counts["borrow_suggestion"],
             counts["applied"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
