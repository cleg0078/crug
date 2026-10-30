#!/usr/bin/env python3
# Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.
"""Unit tests for the c2crug migration tool.

Run directly: python3 scripts/test-c2crug.py
The tests pin the conservative contract of ADR 0023: only a structurally
proven single release is rewritten, everything ambiguous stays raw and is
left byte-identical, and --analyze never changes a file.
"""

import contextlib
import importlib.machinery
import importlib.util
import io
import json
import os
import sys
import tempfile
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOL = os.path.join(ROOT, "c2crug")
VALIDATOR = os.path.join(ROOT, "scripts", "validate-c2crug-report.py")

_loader = importlib.machinery.SourceFileLoader("c2crug_mod", TOOL)
_spec = importlib.util.spec_from_loader(_loader.name, _loader)
c2crug = importlib.util.module_from_spec(_spec)
_loader.exec_module(c2crug)

PASSED = 0


def check(condition, message):
    global PASSED
    if not condition:
        raise AssertionError(message)
    PASSED += 1


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(textwrap.dedent(text).lstrip("\n"))


def run(mode, root, out=None, report=None):
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer), \
            contextlib.redirect_stderr(buffer):
        if mode == "analyze":
            rc = c2crug.cmd_analyze(root, report)
        else:
            rc = c2crug.cmd_safe(root, out, report, False)
    return rc, buffer.getvalue()


def case(name):
    def wrap(fn):
        try:
            fn()
            print("ok   %s" % name)
        except AssertionError as exc:
            print("FAIL %s: %s" % (name, exc))
            raise SystemExit(1)
        return fn
    return wrap


@case("sidecar parsing")
def test_sidecar():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "api.crugffi"), """
            crugffi 1.0;
            fn make returns owned Widget drop destroy_widget;
            fn destroy_widget consumes arg0;
            fn peek borrows arg0;
        """)
        owned, consumes = c2crug.load_sidecars(tmp)
        check(owned.get("make") == "destroy_widget", "owned return not recorded")
        check(consumes.get("destroy_widget") == {0}, "consumes not recorded")
        check(consumes.get("peek") == set(), "borrows must not be consumed")


@case("proven heap value becomes own and drop")
def test_proven():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            #include <stdlib.h>
            int f (int n)
            {
              int *p = malloc (n * sizeof (int));
              p[0] = 1;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1 and entries[0]["kind"] == "own",
              "expected one own entry, got %r" % entries)
        check(entries[0]["applied"] and entries[0]["confidence"] == 1.0,
              "an own entry must be applied with confidence 1.0")
        check("own int *p" in transformed, "own not inserted")
        check("drop (p)" in transformed, "free not rewritten")
        source = c2crug.read_text(os.path.join(tmp, "a.c"))
        check(len(source.split("\n")) == len(transformed.split("\n")),
              "the line count changed")


@case("unknown origin stays raw and untouched")
def test_unknown():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              char *p = make_message ();
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1 and entries[0]["kind"] == "raw",
              "unknown origin must be raw")
        check(not entries[0]["applied"], "raw must not be applied")
        check(transformed is None, "raw declarations must not be rewritten")


@case("conditional release stays raw")
def test_conditional():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int c)
            {
              int *p = malloc (4);
              if (c)
                free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "conditional release must be raw")
        check("conditional path" in entries[0]["reason"],
              "reason must name the conditional path")
        check(transformed is None, "conditional release must not be rewritten")


@case("double release stays raw")
def test_double_release():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "double release must be raw")
        check("more than once" in entries[0]["reason"],
              "reason must say the pointer is released twice")
        check(transformed is None, "double release must not be rewritten")


@case("alias stays raw")
def test_alias():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              int *q = p;
              free (p);
              return 0;
            }
        """)
        entries, _ = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "alias must be raw")
        check("copied into another variable" in entries[0]["reason"],
              "reason must name the alias")


@case("use after release stays raw")
def test_use_after_release():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              p[0] = 1;
              return 0;
            }
        """)
        entries, _ = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "use after release must be raw")
        check("after its release" in entries[0]["reason"],
              "reason must say the pointer is used after its release")


@case("label before a conditional release stays raw")
def test_label_conditional():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int c)
            {
              int *p = malloc (4);
              if (c)
              label:
                free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw",
              "a labelled conditional release must be raw")
        check("conditional path" in entries[0]["reason"],
              "reason must name the conditional path")
        check("label" in entries[0]["reason"],
              "reason must name the label or goto target")
        check(transformed is None, "a labelled release must not be rewritten")


@case("else branch release stays raw")
def test_else_release():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int c)
            {
              int *p = malloc (4);
              if (c)
                p[0] = 1;
              else
                free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "an else release must be raw")
        check("conditional path" in entries[0]["reason"],
              "reason must name the conditional path")
        check("else" in entries[0]["reason"], "reason must name the else body")
        check(transformed is None, "an else release must not be rewritten")


@case("switch case release stays raw")
def test_case_release():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int c)
            {
              int *p = malloc (4);
              switch (c)
                {
                case 1:
                  free (p);
                  break;
                default:
                  break;
                }
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw", "a switch case release must be raw")
        check("conditional path" in entries[0]["reason"],
              "reason must name the conditional path")
        check("switch" in entries[0]["reason"], "reason must name the switch")
        check(transformed is None, "a switch case release must not be rewritten")


@case("brace-less loop body release stays raw")
def test_unbraced_loop_release():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int n)
            {
              int *p = malloc (4);
              int i;
              for (i = 0; i < n; i++)
                free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw",
              "a release in a brace-less loop must be raw")
        check("conditional path" in entries[0]["reason"],
              "reason must name the conditional path")
        check("for" in entries[0]["reason"], "reason must name the loop")
        check(transformed is None, "a loop body release must not be rewritten")


@case("null guard only covers its own early exit (R4-TOOL-002)")
def test_unrelated_early_exit():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (int c)
            {
              int *p = malloc (4);
              if (!p)
                return 1;
              if (c)
                return 2;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw",
              "an unguarded early exit must keep the release raw")
        check("early exit" in entries[0]["reason"],
              "reason must name the early exit")
        check(transformed is None, "an unguarded early exit must not rewrite")


@case("a real null guard is accepted with confidence 0.80")
def test_valid_guard():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              if (!p)
                return 1;
              p[0] = 1;
              free (p);
              return 0;
            }

            int braced (void)
            {
              int *p = malloc (8);
              if (NULL == p) {
                return 1;
              }
              p[0] = 1;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 2, "expected two entries, got %r" % entries)
        check(entries[0]["kind"] == "own" and entries[0]["applied"],
              "the guarded release must be applied")
        check(entries[0]["confidence"] == 0.80,
              "a guarded proof must have confidence 0.80")
        check("null guard" in entries[0]["reason"],
              "reason must name the null guard")
        check(entries[1]["kind"] == "own" and entries[1]["confidence"] == 0.80,
              "the braced guard must be associated with its return")
        check("own int *p" in transformed, "own not inserted")
        check("drop (p)" in transformed, "free not rewritten")


@case("a NULL guard survives preprocessing and stays eligible")
def test_expanded_null_guard():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            #include <stdlib.h>

            int f (void)
            {
              int *p = malloc (4);
              if (p == NULL)
                return 1;
              p[0] = 1;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1, "expected one entry, got %r" % entries)
        entry = entries[0]
        check(entry["kind"] == "own" and entry["applied"],
              "the expanded NULL guard must stay eligible: %r" % entry)
        check(entry["confidence"] == 0.80,
              "the expanded NULL guard must give confidence 0.80")
        check(entry["preprocessed"] is True,
              "the entry must record a successful preprocessing run")
        check(entry["eligibility"] == "eligible",
              "a macro that only spells NULL must not make the body "
              "ineligible")
        check("null guard" in entry["reason"],
              "reason must name the null guard: %r" % entry["reason"])
        check("own int *p" in transformed, "own not inserted")
        check("drop (p)" in transformed, "free not rewritten")


@case("a guard with an else branch does not authorize the release")
def test_guard_with_else():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              if (!p)
                return 1;
              else
                p[0] = 1;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw",
              "a guard with an else branch must not authorize the release")
        check("early exit" in entries[0]["reason"],
              "reason must name the unproven early exit")
        check(transformed is None, "the release must not be rewritten")


@case("multiple declarators stay raw and keep their commas")
def test_multiple_declarators():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int f (void)
            {
              char *p = malloc (4), *q = malloc (8);
              free (p);
              free (q);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(entries[0]["kind"] == "raw",
              "a multiple-declarator line must be raw")
        check("multiple declarators" in entries[0]["reason"],
              "reason must name the multiple declarators")
        check(transformed is None,
              "a multiple-declarator declaration must not be rewritten")


@case("confidence separates unconditional and guarded proofs")
def test_confidence():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            int plain (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }

            int guarded (void)
            {
              int *p = malloc (8);
              if (p == NULL)
                return 1;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 2, "expected two entries, got %r" % entries)
        check(entries[0]["kind"] == "own" and entries[0]["confidence"] == 1.0,
              "an unconditional proof must keep confidence 1.00")
        check(entries[1]["kind"] == "own" and entries[1]["applied"],
              "the guarded proof must be applied")
        check(entries[1]["confidence"] == 0.80,
              "a guarded proof must have confidence 0.80")
        check(transformed is not None, "both functions must be rewritten")


@case("analyze changes nothing")
def test_analyze():
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "src")
        write(os.path.join(root, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        before = c2crug.read_text(os.path.join(root, "a.c"))
        report = os.path.join(tmp, "report.json")
        rc, output = run("analyze", root, report=report)
        check(rc == 0, "analyze failed: %s" % output)
        after = c2crug.read_text(os.path.join(root, "a.c"))
        check(before == after, "analyze changed a source file")
        data = json.load(open(report))
        check(data["mode"] == "analyze" and data["counts"]["applied"] == 0,
              "analyze reported an applied entry")


@case("safe copies and leaves other files identical")
def test_safe_copy():
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "src")
        write(os.path.join(root, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        write(os.path.join(root, "Makefile"), "all:\n\ttrue\n")
        write(os.path.join(root, "notes.txt"), "keep me\n")
        out = os.path.join(tmp, "out")
        report = os.path.join(tmp, "report.json")
        rc, output = run("safe", root, out=out, report=report)
        check(rc == 0, "safe failed: %s" % output)
        check(c2crug.read_text(os.path.join(out, "notes.txt")) == "keep me\n",
              "a non-source file was changed")
        check("own int *p" in c2crug.read_text(os.path.join(out, "a.c")),
              "the proven pointer was not migrated")
        data = json.load(open(report))
        check(data["mode"] == "safe" and data["counts"]["applied"] == 1,
              "safe report must record one applied entry")


@case("report validator accepts a real report and rejects a lie")
def test_validator():
    sys.path.insert(0, os.path.join(ROOT, "scripts"))
    spec = importlib.util.spec_from_file_location("validator", VALIDATOR)
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "src")
        write(os.path.join(root, "a.c"), """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        report = os.path.join(tmp, "report.json")
        rc, output = run("safe", root, out=os.path.join(tmp, "out"),
                         report=report)
        check(rc == 0, "safe failed: %s" % output)
        data = json.load(open(report))
        check(validator.check(data) == [], "the validator rejected a real report")
        lie = json.loads(json.dumps(data))
        lie["entries"][0]["kind"] = "raw"
        check(validator.check(lie), "the validator accepted a raw entry marked applied")
        lie = json.loads(json.dumps(data))
        lie["entries"][0]["confidence"] = 0
        check(validator.check(lie), "the validator accepted a zero-confidence own entry")

@case("macro expansion inside a body makes the function ineligible")
def test_macro_expansion():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            #include <stdlib.h>

            #define BAIL() return 0

            int f (void)
            {
              int *p = malloc (4);
              p[0] = 1;
              free (p);
              BAIL ();
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1, "expected one entry, got %r" % entries)
        entry = entries[0]
        check(entry["kind"] == "raw", "a macro-using body must stay raw")
        check(entry["confidence"] == 0,
              "an ineligible entry needs confidence 0")
        check(entry["preprocessed"] is True,
              "the preprocessing verdict must be recorded")
        check(entry["eligibility"] == "ineligible",
              "a macro-using body must be ineligible")
        check("macro expansion inside the function body" in entry["reason"],
              "reason must name the macro expansion: %s" % entry["reason"])
        check(transformed is None,
              "an ineligible body must not be rewritten")


@case("a macro used only outside the body keeps the function eligible")
def test_macro_outside_body():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            #include <stdlib.h>

            #define UNUSED_MARKER 1

            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1, "expected one entry, got %r" % entries)
        entry = entries[0]
        check(entry["kind"] == "own" and entry["applied"],
              "a macro-free body must stay migratable")
        check(entry["confidence"] == 1.0,
              "an unconditional proof must keep confidence 1.00")
        check(entry["preprocessed"] is True and
              entry["eligibility"] == "eligible",
              "the function must be eligible")
        check(transformed is not None, "the eligible body must be rewritten")


@case("the three audit witnesses are ineligible")
def test_audit_witnesses():
    for name in ("hidden-alias.c", "nested-return.c", "free-redefinition.c"):
        path = os.path.join(ROOT, "migration", "witnesses", name)
        entries, transformed = c2crug.analyse_source(path, name, {}, {})
        own = [e for e in entries if e["kind"] == "own"]
        check(not own, "%s must not produce an own entry: %r" % (name, own))
        check(transformed is None, "%s must not be rewritten" % name)
        raw = [e for e in entries if e["kind"] == "raw"]
        check(raw, "%s must report its pointer declarations: %r"
              % (name, entries))
        for entry in raw:
            check(entry["confidence"] == 0,
                  "%s: raw entry with confidence %r"
                  % (name, entry["confidence"]))
            check(entry["eligibility"] == "ineligible",
                  "%s: raw entry not ineligible" % name)
            check("macro expansion inside the function body"
                  in entry["reason"],
                  "%s: reason does not name the macro expansion: %s"
                  % (name, entry["reason"]))


@case("sidecar header major version is enforced")
def test_sidecar_header():
    with tempfile.TemporaryDirectory() as tmp:
        sidecar = os.path.join(tmp, "api.crugffi")
        write(sidecar, """
            crugffi 99.0;
            fn make returns owned Widget drop destroy_widget;
            fn destroy_widget consumes arg0;
        """)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            owned, consumes = c2crug.load_sidecars(tmp)
        check(owned == {} and consumes == {},
              "a 99.0 sidecar must not be used")
        check("ignoring sidecar" in err.getvalue(),
              "an ignored sidecar must be reported on stderr: %r"
              % err.getvalue())
        write(sidecar, """
            crugffi one;
            fn make returns owned Widget drop destroy_widget;
        """)
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            owned, consumes = c2crug.load_sidecars(tmp)
        check(owned == {} and consumes == {},
              "a malformed sidecar must not be used")
        check("ignoring sidecar" in err.getvalue(),
              "a malformed sidecar must be reported on stderr")
        write(sidecar, """
            crugffi 1.2;
            fn make returns owned Widget drop destroy_widget;
            fn destroy_widget consumes arg0;
        """)
        with contextlib.redirect_stderr(io.StringIO()):
            owned, consumes = c2crug.load_sidecars(tmp)
        check(owned.get("make") == "destroy_widget",
              "a 1.x sidecar must stay usable")
        check(consumes.get("destroy_widget") == {0},
              "a 1.x sidecar must keep its consumes clauses")


@case("a rejected sidecar never credits an own proof")
def test_sidecar_evidence():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "api.crugffi"), """
            crugffi 99.0;
            fn make returns owned Widget drop destroy_widget;
            fn destroy_widget consumes arg0;
        """)
        write(os.path.join(tmp, "a.c"), """
            char *make (void);

            int f (void)
            {
              char *p = make ();
              free (p);
              return 0;
            }
        """)
        with contextlib.redirect_stderr(io.StringIO()):
            owned, consumes = c2crug.load_sidecars(tmp)
        entries, _ = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", owned, consumes)
        check(len(entries) == 1 and entries[0]["kind"] == "raw",
              "a rejected sidecar must leave the origin unknown: %r"
              % entries)
        check("unknown ownership origin" in entries[0]["reason"],
              "reason must say the origin is unknown")


@case("an unusable preprocessor keeps every function ineligible")
def test_preprocess_failure():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "a.c")
        write(path, """
            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        preproc = c2crug.preprocess_source(path, "/nonexistent/c2cpp")
        entries, transformed = c2crug.analyse_source(
            path, "a.c", {}, {}, preproc)
        check(len(entries) == 1 and entries[0]["kind"] == "raw",
              "a failed preprocessing run must keep the entry raw")
        check(entries[0]["preprocessed"] is False,
              "the failed preprocessing run must be recorded")
        check(entries[0]["eligibility"] == "ineligible",
              "a failed preprocessing run must make the body ineligible")
        check("could not be preprocessed" in entries[0]["reason"],
              "reason must say the body could not be preprocessed")
        check(transformed is None, "nothing may be rewritten")


@case("two exits on one physical line stay raw (R4-TOOL-003)")
def test_same_line_exits():
    with tempfile.TemporaryDirectory() as tmp:
        write(os.path.join(tmp, "a.c"), """
            #include <stdlib.h>

            int f (int bail)
            {
              int *p = malloc (4);
              if (p == 0) return 1; if (bail) return 2;
              free (p);
              return 0;
            }
        """)
        entries, transformed = c2crug.analyse_source(
            os.path.join(tmp, "a.c"), "a.c", {}, {})
        check(len(entries) == 1, "expected one entry, got %r" % entries)
        check(entries[0]["kind"] == "raw",
              "the second exit on the line must not borrow the guard")
        check("early exit" in entries[0]["reason"],
              "reason must name the uncovered early exit: %s"
              % entries[0]["reason"])
        check(transformed is None, "the release must not be rewritten")


@case("report entries carry the preprocessing verdict")
def test_report_preprocessing_keys():
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "src")
        write(os.path.join(root, "a.c"), """
            #include <stdlib.h>

            int f (void)
            {
              int *p = malloc (4);
              free (p);
              return 0;
            }
        """)
        report = os.path.join(tmp, "report.json")
        rc, output = run("safe", root, out=os.path.join(tmp, "out"),
                         report=report)
        check(rc == 0, "safe failed: %s" % output)
        data = json.load(open(report))
        for entry in data["entries"]:
            check(isinstance(entry.get("preprocessed"), bool),
                  "entry lacks a boolean preprocessed: %r" % entry)
            check(entry.get("eligibility") in ("eligible", "ineligible"),
                  "entry lacks a valid eligibility: %r" % entry)
        own = [e for e in data["entries"] if e["kind"] == "own"]
        check(own and own[0]["preprocessed"] is True and
              own[0]["eligibility"] == "eligible",
              "an own entry must be eligible and preprocessed")



@case("--cpp and --cpp-arg reach the preprocessor")
def test_cpp_options():
    import shutil
    cc = shutil.which("cc")
    check(cc is not None, "no cc on PATH")
    with tempfile.TemporaryDirectory() as tmp:
        root = os.path.join(tmp, "src")
        path = os.path.join(root, "a.c")
        write(path, """
            #include <stdlib.h>

            int
            f (void)
            {
              int *p = malloc (4);
              EXTRA ();
              free (p);
              return 0;
            }
        """)
        report = os.path.join(tmp, "r.json")
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), \
                contextlib.redirect_stderr(buffer):
            rc = c2crug.main(["--analyze", root, "--cpp", cc,
                              "--report", report])
        check(rc == 0, "--analyze with --cpp failed: %s" % buffer.getvalue())
        with open(report, "r", encoding="utf-8") as f:
            data = json.load(f)
        own = [e for e in data["entries"] if e["kind"] == "own"]
        check(own and own[0]["confidence"] == 1.0,
              "--cpp must select a working preprocessor: %r"
              % data["entries"])
        preproc = c2crug.preprocess_source(
            path, cc, ("-DEXTRA()=return 7;",))
        check(preproc["ok"], "the defining preprocessing run failed")
        entries, transformed = c2crug.analyse_source(
            path, "a.c", {}, {}, preproc)
        check(entries and entries[0]["eligibility"] == "ineligible",
              "--cpp-arg must forward the define: %r" % entries)
        check(entries[0]["kind"] == "raw" and
              entries[0]["confidence"] == 0.0,
              "a macro that adds an exit must stay raw at confidence 0")
        check(entries[0]["reason"] ==
              "macro expansion inside the function body",
              "reason must name the macro expansion: %s"
              % entries[0]["reason"])
        check(transformed is None, "nothing may be rewritten")


@case("_Exit, siglongjmp and pthread_exit are control-flow exits")
def test_exit_calls():
    for call in ("_Exit (0)", "siglongjmp (jb, 1)", "pthread_exit (0)",
                 "pthread_cancel (t)", "thrd_exit (0)"):
        with tempfile.TemporaryDirectory() as tmp:
            write(os.path.join(tmp, "a.c"), """
                int f (int c)
                {
                  int *p = malloc (4);
                  if (c) CALL;
                  free (p);
                  return 0;
                }
            """.replace("CALL", call))
            entries, transformed = c2crug.analyse_source(
                os.path.join(tmp, "a.c"), "a.c", {}, {})
            check(len(entries) == 1 and entries[0]["kind"] == "raw",
                  "calling %s must leave the release conditional: %r"
                  % (call, entries))
            check("early exit" in entries[0]["reason"],
                  "%s: reason must name the uncovered early exit: %s"
                  % (call, entries[0]["reason"]))
            check(transformed is None,
                  "%s: the release must not be rewritten" % call)


@case("a declared noreturn wrapper is a control-flow exit")
def test_noreturn_exit():
    for decl in ("void bail (void) __attribute__ ((noreturn));",
                 "__attribute__ ((noreturn)) void bail (void);",
                 "_Noreturn void bail (void);"):
        with tempfile.TemporaryDirectory() as tmp:
            write(os.path.join(tmp, "a.c"), """
                DECL
                int COND;
                int f (void)
                {
                  int *p = malloc (4);
                  if (COND) bail ();
                  free (p);
                  return 0;
                }
            """.replace("DECL", decl))
            entries, transformed = c2crug.analyse_source(
                os.path.join(tmp, "a.c"), "a.c", {}, {})
            check(len(entries) == 1 and entries[0]["kind"] == "raw",
                  "a call to a noreturn function must leave the release "
                  "conditional: %r" % entries)
            check("early exit" in entries[0]["reason"],
                  "%r: reason must name the early exit: %s"
                  % (decl, entries[0]["reason"]))
            check(transformed is None, "nothing may be rewritten")


@case("#line directives make the body ineligible with an honest reason")
def test_line_directives():
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "a.c")
        write(path, """
            int f (void)
            {
              int *p = malloc (4);
            #line 99 "a.c"
              free (p);
              return 0;
            }
        """)
        preproc = c2crug.preprocess_source(path)
        check(not preproc["ok"],
              "a source #line directive must not be mapped silently")
        check("line directives" in preproc["error"],
              "the preprocessing error must name the directives: %r"
              % preproc["error"])
        entries, transformed = c2crug.analyse_source(
            path, "a.c", {}, {}, preproc)
        check(len(entries) == 1 and entries[0]["kind"] == "raw",
              "a #line body must stay raw: %r" % entries)
        check(entries[0]["eligibility"] == "ineligible",
              "a #line body must be ineligible")
        check("line directives" in entries[0]["reason"],
              "reason must name the directives: %s" % entries[0]["reason"])
        check(transformed is None, "nothing may be rewritten")


@case("command line")
def test_command_line():
    buffer = io.StringIO()
    with contextlib.redirect_stdout(buffer):
        rc = c2crug.main(["--version"])
    out = buffer.getvalue().strip()
    check(rc == 0, "--version exited %d" % rc)
    check(out == "c2crug %s" % c2crug.VERSION,
          "--version printed %r" % out)
    buffer = io.StringIO()
    with contextlib.redirect_stderr(buffer):
        try:
            c2crug.main([])
        except SystemExit as exc:
            code = exc.code
        else:
            code = None
    check(code == 2, "a run without a mode exited %r, want 2" % (code,))
    check("--analyze" in buffer.getvalue(),
          "the missing-mode error does not name the modes")


def main():
    print("test-c2crug: all %d checks pass" % PASSED)


if __name__ == "__main__":
    main()
