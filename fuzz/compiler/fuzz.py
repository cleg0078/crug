#!/usr/bin/env python3
"""fuzz.py - deterministic mutational fuzz harness for the Crug compiler.

Phase 22 deliverable.  Python 3 standard library only; no third-party
packages.  For every input the harness runs the compiler with
-fsyntax-only -fno-diagnostics-color plus the flags of the selected target,
with a 10 second timeout, and classifies the result:

  crash    negative return code (killed by a signal / SIGSEGV) or stderr
           containing "internal compiler error" or "Segmentation fault"
  timeout  the per-input timeout expired
  ok       any other return code, including ordinary Crug rejections

Every unique crash/timeout signature (signal or matched message plus the
normalized top stderr line) is saved under <findings>/<target>/ and printed.

Targets:
  parser     -fcrug-ownership   the contextual parser / ownership grammar
  ownership  -fcrug-ownership   move/use/adopt/drop semantics
  borrow     -fcrug-borrow      shared/mut borrow and last-use rules
  ffi        -fcrug-ffi=<sidecar>
                                the .crugffi parser; the sidecar is mutated
                                next to a fixed driver and header

The PRNG is seeded (default 20261020), the corpus is read in sorted order
and every mutation consumes randomness deterministically, so two runs with
the same seed, corpus and budget produce the same inputs and the same
output lines.
"""

import argparse
import hashlib
import os
import random
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DEFAULT_COMPILER = ROOT / "gcc" / "crugc"
DEFAULT_CORPUS = HERE / "corpus"
DEFAULT_FINDINGS = ROOT / "build" / "fuzz" / "findings"
DEFAULT_SEED = 20261020
DEFAULT_BUDGET = 300
PER_INPUT_TIMEOUT = 10.0
MAX_INPUT = 8192

TARGETS = ("parser", "ownership", "borrow", "ffi")
TARGET_FLAGS = {
    "parser": ["-fcrug-ownership"],
    "ownership": ["-fcrug-ownership"],
    "borrow": ["-fcrug-borrow"],
    # ffi: -fcrug-ffi=<sidecar>, built per input
}

# Token dictionary for C/Crug inputs.  Entries may be more than one token;
# token-level replacement is allowed to change the token count.
C_DICT = [
    b"own", b"ref", b"mut", b"move", b"borrow", b"borrow_mut", b"adopt",
    b"release", b"drop", b"struct", b"union", b"enum", b"typedef", b"static",
    b"extern", b"const", b"volatile", b"void", b"int", b"char", b"unsigned",
    b"long", b"return", b"if", b"else", b"while", b"for", b"do", b"switch",
    b"case", b"sizeof", b"_Static_assert", b"__attribute__", b"NULL", b"0",
    b"(void *) 0", b"(int *) 0", b"*", b"&", b";", b"{", b"}", b"(", b")",
    b"[", b"]", b"=", b"==", b"!=", b"->", b".", b",", b":", b"?", b"++",
    b"--", b"||", b"&&", b"<", b">", b"<=", b">=", b"+", b"-", b"!", b"~",
    b"\"x\"", b"'c'", b"/*x*/", b"//x\n", b"\\n",
]

# Token dictionary for .crugffi sidecars: the grammar words of ADR 0007.
FFI_DICT = [
    b"crugffi", b"1.0", b";", b"fn", b"returns", b"owned", b"nullable",
    b"drop", b"consumes", b"arg0", b"arg1", b"arg2", b"borrows",
    b"mut_borrows", b"out_owned", b"callback", b"void", b"int", b"FILE",
    b"Node", b"FuzzNode", b"my_alloc", b"my_free", b"fuzz_alloc",
    b"fuzz_free", b"fclose", b"free", b"malloc", b"0", b"-1", b",", b".",
    b"=", b"(", b")", b"[", b"]", b"{", b"}", b"#x\n", b"\n\n",
    b"returns owned void nullable drop my_free",
    b"consumes arg0 nullable",
]

# Fixed FFI driver (C) and API header.  The fuzz input is the sidecar that
# sits next to them; mutations never touch these two files.  The driver
# includes the header and calls declared functions so the sidecar's
# annotations are exercised by the FFI contract checker.
FFI_HEADER = b"""#ifndef CRUG_FUZZ_FFI_API_H
#define CRUG_FUZZ_FFI_API_H
#include <stdio.h>
#include <stdlib.h>

typedef struct Node { int v; } Node;
typedef struct FuzzNode { int v; } FuzzNode;

extern Node *my_alloc (int v);
extern void my_free (Node *n);
extern void fill_node (Node **out);
extern int peek (Node *n);
extern void set_value (Node *n, int v);
extern void run_cb (void (*fn) (void));
extern int live_count (void);

extern FuzzNode *fuzz_alloc (int v);
extern void fuzz_free (FuzzNode *n);
extern void fuzz_fill (FuzzNode **out);
extern int fuzz_peek (FuzzNode *n);
extern void fuzz_set (FuzzNode *n, int v);
extern void fuzz_cb (void (*fn) (void));
extern int fuzz_count (void);

#endif
"""

FFI_DRIVER = b"""#include "ffi_api.h"

int main (void)
{
  own Node *n = my_alloc (1);
  peek (n);
  my_free (move (n));

  own FILE *f = fopen ("/dev/null", "r");
  fclose (move (f));

  own void *p = malloc (8);
  free (move (p));

  return 0;
}
"""

TOKEN_RE = re.compile(
    rb"[A-Za-z_][A-Za-z0-9_]*|->|\+\+|--|&&|\|\||==|!=|<=|>=|\S"
)
ABS_PATH_RE = re.compile(r"(?<![\w.:-])/(?:[A-Za-z0-9_.+-]+/)*[A-Za-z0-9_.+-]+")


# --------------------------------------------------------------------------
# corpus and mutations
# --------------------------------------------------------------------------

def seed_suffix(target):
    return ".crugffi" if target == "ffi" else ".c"


def load_corpus(corpus_root, target):
    directory = corpus_root / target
    if not directory.is_dir():
        raise SystemExit("fuzz: corpus directory missing: %s" % directory)
    suffix = seed_suffix(target)
    files = sorted(p for p in directory.iterdir()
                   if p.is_file() and p.name.endswith(suffix))
    seeds = []
    for path in files:
        data = path.read_bytes()
        if not data:
            raise SystemExit("fuzz: empty corpus seed: %s" % path)
        seeds.append((path.name, data[:MAX_INPUT]))
    if not seeds:
        raise SystemExit("fuzz: no %s seeds under %s" % (suffix, directory))
    return seeds


def op_splice(data, other, rng):
    """Replace a random byte range with a chunk from another corpus file."""
    if not other:
        return data
    start = rng.randrange(len(other))
    length = rng.randint(1, min(96, len(other) - start))
    chunk = other[start:start + length]
    pos = rng.randint(0, len(data))
    end = min(len(data), pos + rng.randint(1, 96))
    return data[:pos] + chunk + data[end:]


def op_duplicate_lines(data, rng):
    lines = data.splitlines(keepends=True)
    if not lines:
        return b"\n"
    index = rng.randrange(len(lines))
    count = rng.randint(1, min(3, len(lines) - index))
    block = lines[index:index + count]
    insert = rng.randint(0, len(lines))
    return b"".join(lines[:insert] + block + lines[insert:])


def op_delete_lines(data, rng):
    lines = data.splitlines(keepends=True)
    if len(lines) < 2:
        return op_splice(data, b"", rng)
    index = rng.randrange(len(lines))
    del lines[index]
    return b"".join(lines)


def op_token(data, rng, dictionary):
    matches = list(TOKEN_RE.finditer(data))
    if not matches:
        return data + b" " + rng.choice(dictionary)
    if rng.random() < 0.5:
        match = matches[rng.randrange(len(matches))]
        return data[:match.start()] + rng.choice(dictionary) + data[match.end():]
    first = rng.randrange(len(matches))
    second = rng.randrange(len(matches))
    if first == second:
        return data
    if first > second:
        first, second = second, first
    a = matches[first]
    b = matches[second]
    return (data[:a.start()] + b.group() + data[a.end():b.start()]
            + a.group() + data[b.end():])


def op_truncate(data, rng):
    if len(data) < 2:
        return data
    return data[:rng.randint(1, len(data) - 1)]


def op_insert_token(data, rng, dictionary):
    pos = rng.randint(0, len(data))
    return data[:pos] + rng.choice(dictionary) + b" " + data[pos:]


def mutate(data, other, rng, dictionary):
    data = data[:MAX_INPUT]
    for _ in range(rng.randint(1, 4)):
        op = rng.randrange(6)
        if op == 0:
            data = op_splice(data, other, rng)
        elif op == 1:
            data = op_duplicate_lines(data, rng)
        elif op == 2:
            data = op_delete_lines(data, rng)
        elif op == 3:
            data = op_token(data, rng, dictionary)
        elif op == 4:
            data = op_truncate(data, rng)
        else:
            data = op_insert_token(data, rng, dictionary)
        if len(data) > MAX_INPUT:
            data = data[:MAX_INPUT]
    return data or b"\n"


# --------------------------------------------------------------------------
# running the compiler and classifying the result
# --------------------------------------------------------------------------

def run_compiler(compiler, flags, source, timeout):
    """Run one compile.  Returns (returncode, stderr_bytes, timed_out)."""
    command = [str(compiler), "-fsyntax-only", "-fno-diagnostics-color"]
    command.extend(flags)
    command.append(str(source))
    proc = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )
    try:
        _stdout, stderr = proc.communicate(timeout=timeout)
        return proc.returncode, stderr, False
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        _stdout, stderr = proc.communicate()
        return proc.returncode, stderr, True


def decode(data):
    return data.decode("utf-8", "replace")


def first_meaningful_line(text):
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return "<no stderr>"


def normalize_text(text, redactions):
    for item in redactions:
        text = text.replace(str(item), "<work>")
    text = ABS_PATH_RE.sub("<path>", text)
    text = re.sub(r"\b0x[0-9a-fA-F]+\b", "0xADDR", text)
    return re.sub(r"\s+", " ", text).strip()


def classify(returncode, stderr, timed_out, redactions):
    """Return (kind, signature-detail) where kind is crash/timeout/ok."""
    text = decode(stderr)
    if timed_out:
        top = normalize_text(first_meaningful_line(text), redactions)
        return "timeout", "timeout after %.0fs | %s" % (PER_INPUT_TIMEOUT, top)
    lowered = text.lower()
    matched = None
    if "internal compiler error" in lowered:
        matched = "internal compiler error"
    elif "segmentation fault" in lowered:
        matched = "Segmentation fault"
    if matched is None and returncode >= 0:
        return "ok", ""
    reason = matched if matched is not None else "signal %d" % (-returncode)
    top = normalize_text(first_meaningful_line(text), redactions)
    return "crash", "%s | %s" % (reason, top)


# --------------------------------------------------------------------------
# findings
# --------------------------------------------------------------------------

def input_hash(target, data):
    digest = hashlib.sha256(target.encode("ascii") + b"\0" + data).hexdigest()
    return digest[:16]


def write_finding(findings_root, target, data, kind, detail, returncode,
                  stderr, redactions, sidecar_data=None):
    """Save one unique finding; returns the primary artifact path."""
    outdir = findings_root / target
    outdir.mkdir(parents=True, exist_ok=True)
    tag = input_hash(target, data)
    if target == "ffi":
        source_name = tag + ".c"
        header_name = tag + ".h"
        sidecar_name = tag + ".crugffi"
        driver = FFI_DRIVER.replace(
            b'#include "ffi_api.h"',
            ('#include "%s"' % header_name).encode("ascii"),
        )
        (outdir / source_name).write_bytes(driver)
        (outdir / header_name).write_bytes(FFI_HEADER)
        (outdir / sidecar_name).write_bytes(sidecar_data if sidecar_data
                                            is not None else data)
        tag_note = " input=%s sidecar=%s" % (source_name, sidecar_name)
        repro_flags = "-fcrug-ffi=%s" % sidecar_name
        repro_source = source_name
    else:
        source_name = tag + ".c"
        (outdir / source_name).write_bytes(data)
        tag_note = " input=%s" % source_name
        repro_flags = " ".join(TARGET_FLAGS[target])
        repro_source = source_name

    log_line = (
        "target=%s kind=%s signature=%s exit=%d%s flags=%s"
        % (target, kind, detail, returncode, tag_note, repro_flags)
    )
    with (outdir / "crashes.log").open("a", encoding="utf-8") as log:
        log.write(log_line + "\n")

    excerpt = normalize_text(decode(stderr)[:8000], redactions)
    details = [
        "target=%s" % target,
        "kind=%s" % kind,
        "signature=%s" % detail,
        "exit=%d" % returncode,
        "reproduce: %s -fsyntax-only -fno-diagnostics-color %s %s"
        % ("gcc/crugc", repro_flags, repro_source),
        "stderr: %s" % (excerpt if excerpt else "<empty>"),
        "",
    ]
    (outdir / (tag + ".log")).write_text("\n".join(details), encoding="utf-8")
    return outdir / source_name


# --------------------------------------------------------------------------
# per-target driver
# --------------------------------------------------------------------------

def run_target(target, args, corpus_root, findings_root, compiler, workdir):
    seeds = load_corpus(corpus_root, target)
    dictionary = FFI_DICT if target == "ffi" else C_DICT
    rng = random.Random("%s:%s" % (args.seed, target))

    sidecar = None
    driver = None
    header = None
    if target == "ffi":
        driver = workdir / "ffi_driver.c"
        header = workdir / "ffi_api.h"
        sidecar = workdir / "sidecar.crugffi"
        driver.write_bytes(FFI_DRIVER)
        header.write_bytes(FFI_HEADER)

    crashes = 0
    timeouts = 0
    seen = set()

    for index in range(args.budget):
        _name, seed = seeds[index % len(seeds)]
        _other_name, other = seeds[rng.randrange(len(seeds))]
        mutant = mutate(seed, other, rng, dictionary)

        if target == "ffi":
            sidecar.write_bytes(mutant)
            source = driver
            flags = ["-fcrug-ffi=%s" % sidecar]
            redactions = [workdir]
        else:
            source = workdir / "input.c"
            source.write_bytes(mutant)
            flags = list(TARGET_FLAGS[target])
            redactions = [workdir]

        returncode, stderr, timed_out = run_compiler(
            compiler, flags, source, PER_INPUT_TIMEOUT
        )
        kind, detail = classify(returncode, stderr, timed_out, redactions)

        if kind == "ok":
            continue
        if kind == "timeout":
            timeouts += 1
        else:
            crashes += 1

        key = (kind, detail)
        if key in seen:
            continue
        seen.add(key)
        artifact = write_finding(
            findings_root, target, mutant, kind, detail, returncode,
            stderr, redactions,
            sidecar_data=mutant if target == "ffi" else None,
        )
        print("fuzz: %s: %s: %s (input saved as %s)"
              % (target, kind.upper(), detail, artifact), flush=True)

    print("fuzz: %s: %d input(s), %d crash(es), %d timeout(s)"
          % (target, args.budget, crashes, timeouts), flush=True)
    return args.budget, crashes, timeouts


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def default_budget():
    raw = os.environ.get("FUZZ_BUDGET")
    if raw is None or raw == "":
        return DEFAULT_BUDGET
    try:
        value = int(raw)
    except ValueError:
        raise SystemExit("fuzz: FUZZ_BUDGET must be an integer, got %r" % raw)
    if value < 0:
        raise SystemExit("fuzz: FUZZ_BUDGET must be non-negative")
    return value


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Deterministic mutational fuzz harness for the Crug compiler.",
    )
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--target", choices=TARGETS, help="fuzz one target")
    group.add_argument("--check", action="store_true",
                       help="fuzz all four targets (default)")
    parser.add_argument("--budget", type=int, default=default_budget(),
                        help="inputs per target (default: FUZZ_BUDGET or %d)"
                             % DEFAULT_BUDGET)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED,
                        help="PRNG seed (default: %d)" % DEFAULT_SEED)
    parser.add_argument("--compiler", default=str(DEFAULT_COMPILER),
                        help="compiler driver (default: %s)" % DEFAULT_COMPILER)
    parser.add_argument("--corpus", default=str(DEFAULT_CORPUS),
                        help="corpus root (default: %s)" % DEFAULT_CORPUS)
    parser.add_argument("--findings", default=str(DEFAULT_FINDINGS),
                        help="findings root (default: %s)" % DEFAULT_FINDINGS)
    args = parser.parse_args(argv)
    if args.budget < 0:
        parser.error("--budget must be non-negative")
    if not args.target:
        args.check = True
    return args


def resolve_path(raw):
    path = Path(raw).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    return path.resolve()


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)

    compiler = resolve_path(args.compiler)
    if not compiler.is_file() or not os.access(str(compiler), os.X_OK):
        print("fuzz: compiler is not executable: %s" % compiler, file=sys.stderr)
        return 2
    corpus_root = resolve_path(args.corpus)
    findings_root = resolve_path(args.findings)

    targets = TARGETS if args.check else (args.target,)
    started = time.monotonic()
    totals = [0, 0, 0]
    with tempfile.TemporaryDirectory(prefix="crug-fuzz-") as tmp:
        workdir = Path(tmp)
        for target in targets:
            inputs, crashes, timeouts = run_target(
                target, args, corpus_root, findings_root, compiler, workdir
            )
            totals[0] += inputs
            totals[1] += crashes
            totals[2] += timeouts

    elapsed = time.monotonic() - started
    print("compiler-fuzz: %d harness(es), %d input(s), %d crash(es), "
          "%d timeout(s)" % (len(targets), totals[0], totals[1], totals[2]))
    if totals[1] or totals[2]:
        print("compiler-fuzz: FAIL")
        return 1
    print("compiler-fuzz: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
