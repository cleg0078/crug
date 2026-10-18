# Architecture Decision Records

Each ADR records one decision that the specification leaves open or contradicts.
Status values: Proposed (not yet ratified), Accepted, Superseded, Rejected.
A semantic change requires a new or amended ADR; editing a spec document alone is
not a decision.

| ADR | Title | Status |
|-----|-------|--------|
| 0002 | Context-sensitive Crug words | Proposed |
| 0004 | Ownership of aggregates, containers and ABI boundaries | Proposed |
| 0005 | Destruction and non-local control flow | Proposed alongside |

The roadmap requires seventeen "required ADRs" but never lists them. This
directory is the natural home; the numbering below is a proposal, not a ratified
plan.

| Candidate | Decision it must settle |
|-----------|-------------------------|
| 0001 | Repository structure and the GCC fork relationship |
| 0002 | Context-sensitive words (written) |
| 0003 | Ownership states and the move/copy model |
| 0004 | Aggregate ownership and ABI (written) |
| 0005 | Destruction and non-local control flow (written) |
| 0006 | Borrow checking and lifetime inference |
| 0007 | Compatibility with C constructs the model cannot express (the `raw` escape) |
| 0008 | FFI sidecar validity and threat model |
| 0009 | Kernel profile |
| 0010 | Aliasing and the noalias permission model |
| 0011 | Concurrency scope semantics and failure propagation |
| 0012 | Diagnostics, warnings and strict mode |
| 0013 | Versioning and feature flags |
| 0014 | Testing and conformance evidence |
| 0015 | Performance methodology |
| 0016 | Source and binary compatibility policy |
| 0017 | Release process for 1.0 |
