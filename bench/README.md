# Benchmarks

This directory holds the CRUG benchmark corpus and its driver. It is empty in
the initial import: Phase 21 (performance qualification) populates it together
with the parity harness.

Rules that apply to every benchmark added here:

- Each input records its provenance and licence in `bench/NOTICE`; per-input
  licences differ, so there is no blanket licence for this directory.
- No benchmark is vendored from a project whose licence forbids it (in
  particular the GPLv2 Linux kernel source is consumed from `sources/`, never
  copied here).
- Results are written under `bench/results/`, which is gitignored; only the
  driver, the corpus index and the recorded summary are committed.
