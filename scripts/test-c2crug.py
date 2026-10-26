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
