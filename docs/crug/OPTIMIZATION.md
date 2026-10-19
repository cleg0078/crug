# Ownership-aware optimization

Phase 11 gives the optimizer one fact that only an ownership system can prove:
a `mut` parameter is an exclusive borrow, and an `own` parameter is the
only owner of its value. Both are compiled as if the parameter said
`restrict`. The decision record is ADR 0010; this file is the user-facing
description and the operator's manual for the checks.

## 1. Turning it on

    crugc -O2 -fcrug-borrow -fcrug-alias-facts program.c

| Flag | Effect |
|------|--------|
| `-fcrug-alias-facts` | compile `mut` and `own` pointer parameters as `restrict`; implies `-fcrug-borrow` and `-fcrug-ownership` |
| `-fcrug-dump=alias` | write the derived facts to `program.crug-alias` |

The flag is **off by default**. It changes code generation and makes an
unproven call site undefined, exactly like a hand-written `restrict`, so it is
opt-in. `-fcrug-modern` does not enable it. Without the flag, a plain pointer
at a `mut` parameter is accepted (the historical leniency of
`BORROW.md` section 2.4) and no fact is emitted.

## 2. What the fact is

    void scale (mut float *d, ref const float *a, unsigned long n)
    {
      for (unsigned long i = 0; i < n; i++)
        d[i] = a[i] * 2;
    }

`d` is an exclusive borrow: the caller cannot hold another borrow of the same
object for the duration of the call. The callee is therefore compiled as

    void scale (float *restrict d, const float *restrict a, unsigned long n)

which lets the vectorizer version and align the loop. `a` is a shared borrow
and gets no fact. A plain pointer parameter gets no fact either.

The fact is applied to the outer pointer type of the parameter. The ABI does not
change: `restrict` is erased at the boundary exactly like the other Crug
qualifiers (ADR 0003, ADR 0006), and a C caller links to a Crug callee with no
shim.

## 3. What the caller must do

With `-fcrug-alias-facts` the compiler verifies the contract it is about to
claim:

| Code | Rejected form |
|------|---------------|
| `crug-BORROW-021` | `set (&x, 1);` where `set` takes `mut int *`: a plain pointer proves no exclusivity |
| `crug-BORROW-022` | passing a shared borrow to a `mut` parameter |
| `crug-BORROW-002` | `f (borrow_mut (x), borrow (x))`: two views of one lender, one mutable |
| `crug-BORROW-003` | `f (borrow_mut (x), borrow_mut (x))`: two mutable borrows of one lender |
| `crug-BORROW-023` | `f (borrow_mut (x), x)`: the lender is passed beside its own mutable borrow |

The correct forms are `borrow_mut (x)` (or a `mut` binding) for a `mut`
parameter and `borrow (x)` (or a `ref` binding) for a `ref` parameter.
Without the flag these checks are not applied, because no fact is claimed.

## 4. The audit surface

`-fcrug-dump=alias` writes one line per derived fact:

    crug alias dump v1
    alias scale: parameter d restrict (mut)

Only compiler-derived facts appear. A user-written `restrict` is not in the
dump, so the dump can be audited against the source without confusing the two.
`scripts/check-stop-conditions.sh` does that: every dumped parameter name must
be declared `mut` or `own` in the source, a ref-only function must produce
no fact, and at least one fact must have been audited. A false fact is a P0.

## 5. The benchmark matrix

`bench/` contains four kernels: `vadd`, `stencil`, `strcopy` and
`list` (pointer chasing as the control). Each has a kernel translation unit
compiled three ways and one shared plain-C `main` translation unit:

| Arm | Kernel parameters | Flags |
|-----|-------------------|-------|
| plain | `int *d, const int *a` | none |
| restrict | `int *restrict d, const int *restrict a` | none |
| crug | `mut int *d, ref const int *a` | `-fcrug-borrow -fcrug-alias-facts` |

Because the main unit is shared, the restrict and crug kernel objects are the
same translation unit with only the qualifiers differing. The driver requires
them to be **byte-identical** at `-O2` and `-O3`; that identity, not the
timing, is what the phase gate adjudicates. It also measures wall-clock time
(minimum of several runs) and reports the ratio, and it runs a `-flto` pass
that requires all three arms to build and agree on the result.

    bench/run.py --matrix alias --check
    bench/run.py --matrix alias --lto
    bench/run.py --record-baseline
    bench/run.py --plain-c --check

One run on the phase-11 host (x86_64, `-O2`, 2026-10-19):

| Kernel | plain | restrict | crug | crug/restrict | kernel objects |
|--------|-------|----------|------|---------------|----------------|
| vadd | 3.99 ms | 1.70 ms | 1.57 ms | 1.000 | identical |
| stencil | 5.76 ms | 1.98 ms | 2.04 ms | 1.000 | identical |
| strcopy | 5.21 ms | 0.35 ms | 0.34 ms | 1.000 | identical |
| list | 1.91 ms | 1.85 ms | 1.79 ms | 1.000 | identical |

The timing of this host is noisy: repeated runs of the same executable have been
observed to differ by more than a factor of two, which is why the gate rests on
object identity and only measures when the objects differ. The plain column is
informational and machine-dependent; it shows what the missing fact costs for
these kernels in this build, not a general claim.

## 6. Plain C is not degraded

The plain arm is compared with `bench/baseline.json`, which records the plain
kernel object digests and timings at the phase-11 commit:

    bench/run.py --plain-c --check

At `-O0`, `-O2` and `-O3` the check requires the plain kernel object to be
byte-identical to the recorded object; a code generation change is then measured
against the 1.02 band. LTO timing is printed but not enforced, because GCC's LTO
object files are not byte-reproducible; the LTO arm is instead required to build
and run correctly by `--matrix alias --lto`. `make check-C_COMPAT` remains
the broader plain-C compatibility gate.

## 7. Limits

- No fact for `ref` or plain pointers: they do not prove exclusivity.
- No interprocedural propagation: the fact belongs to the callee's parameter.
- An unannotated caller is not checked; as with `restrict`, the contract is the
  caller's to keep. Compile callers with the flag to have them checked.
- Owning payload enums inside aggregates, and owned payloads in
  `option`/`result`, are untracked (the phase-10 boundary), so no fact is
  derived from them.
- No new GIMPLE opcode and no vectorizer change; the mechanism is the
  `restrict` qualifier.
- The target is parity with a hand-written `restrict` twin, not beating it
  (RFC section 26).

## 8. Tests

| Command | Asserts |
|---------|---------|
| `make check-ALIAS_FACTS` | 16 DejaGnu cases: acceptance, flag-off leniency, five rejections |
| `scripts/check-optimization.sh` | golden dump, 8/8 kernel objects identical, ref-only identity, flag-off identity, five rejections, runtime |
| `scripts/check-stop-conditions.sh` | every dumped fact names a `mut`/`own` parameter; 0 false-noalias reports |
| `bench/run.py --matrix alias --check` | 5% band with byte identity as primary evidence |
| `bench/run.py --matrix alias --lto` | LTO build, equal results, execution |
| `bench/run.py --plain-c --check` | plain C not degraded (object identity, 1.02 band when code changes) |
| `make check-OPTIMIZATION` | the DejaGnu suite plus the three Python checks |

The fixtures live in `gcc/testsuite/crug.dg/alias/` and
`gcc/testsuite/crug.dg/optimization/`; the benches in `bench/kernels/`.
