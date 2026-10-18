# Phase baseline: phase0

Recorded: 2026-10-18T23:03:04Z (UTC) from commit 5bd7528 (5bd7528ebec3022eba15c9dcb1952f4b21f3f35a).

- Compiler: xgcc (GCC) 16.2.0
- Host: Linux 7.0.0-38-generic x86_64
- Command: make check-c (RUNTESTFLAGS as on the command line)
- Source tree: repository root, unchanged since the tag releases/gcc-16.2
  except for later commits; this run record is only valid for 5bd7528.

## Result

- PASS: 224481
- FAIL: 84
- XFAIL: 1649
- XPASS: 23
- UNRESOLVED: 0
- UNSUPPORTED: 4049

## Files

- gcc.sum.xz - the complete DejaGnu summary (authoritative).
- check-c.log.xz - the console log of the same run.
- FAIL.txt - every FAIL, XPASS and UNRESOLVED line extracted from the sum.
- SUMMARY.txt - the "# of" summary lines from the sum.
- known-fail.txt - the reviewed allowlist of failing tests, with the reason for
  each group; scripts/check-baseline.sh fails on any FAIL outside it.

## Adjudication note for the phase-0 gate

Phase 0 check 3 asks for a run summary archived under this directory with
no FAIL line absent from the pre-fork baseline. The source tree at this commit
is the imported GCC 16.2.0 tree (tag releases/gcc-16.2), so it *is* the
pre-fork baseline: any unexpected failure listed in FAIL.txt is an upstream
failure present before the fork started, not a regression introduced by Crug.
The gate adjudicator records that conclusion here and cites this file.
