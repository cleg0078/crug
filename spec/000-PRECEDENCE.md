# 000 - Specification Precedence and Conflict Register

- **Status:** Proposed (requires owner ratification; see DECISION-REQUIRED items)
- **Date:** 2026-10-18
- **Supersedes:** none
- **Applies to:** every document in spec/ and docs/crug/

## 1. Why this file exists

The project has four specification documents and no rule for which one wins when
they disagree. They do disagree: on partial moves, on context-sensitive words, on
what "production" means, and on licensing. Every disagreement silently becomes an
implementation decision made by whoever writes the parser first. This file fixes
the order of authority, defines what "normative" means in this project, and
registers every known conflict with an owner and a resolution path.

## 2. Authority order

1. **spec/CRUG_RFC_0001.md** - normative for: conformance classes (C0..P1),
   permanent invariants, ABI transparency, zero-cost promises, the kernel profile,
   versioning, and change control. It is the only document that defines what a
   claim of conformance means.
2. **spec/CRUG_MASTER_SPEC_EN.md** - normative for syntax, semantics and driver
   behaviour that the RFC does not state. It is the working language
   specification. Where it conflicts with the RFC, the RFC wins until the RFC is
   amended or the master spec is amended by ADR.
3. **spec/CRUG_RUST_MEMORY_GAPS.md** - requirements input. It uses MUST language
   and is the most detailed semantics text in the repository, but it is never by
   itself normative: a rule from it becomes binding only when merged into the
   master spec or an RFC by ADR.
4. **spec/CRUG_PRODUCTION_ROADMAP.md** - execution plan. It is never normative for
   language semantics. It is normative only for process: phase order, gates,
   severity definitions, and stop conditions.

Tie-break procedure, in order: (a) RFC-0001; (b) an Accepted ADR in
docs/crug/adr/; (c) MASTER_SPEC; (d) MASTER_SPEC plus an ADR that says which
reading was chosen. If none of these settles it, the question is a
DECISION-REQUIRED item and no code may rely on the answer.

## 3. What "normative" means

Only uppercase RFC 2119 keywords carry obligation (RFC-0001 section 1, "Only
uppercase use has normative meaning"). This has two consequences the project has
not yet absorbed:

- Lower-case "must", "required" and "must be measured" in any document are
  informative, including in the roadmap and in RFC-0001 section 56.
- A sentence addressed to a future document ("The concurrency standard MUST
  define ...") creates an obligation on the standards process, not on an
  implementation. It cannot be used to claim a conformance class.

## 4. Conflict register

| ID | Conflict | Documents | Current reading | Owner / resolution |
|----|----------|-----------|-----------------|--------------------|
| C-01 | Partial moves: forbidden in the ownership MVP vs. required as an acceptance criterion of the move-path model | MASTER_SPEC section 13 vs. RUST_MEMORY_GAPS section 5, section 41 | MVP tracks ownership at variable granularity and at top-level pointed-to resource; field-granular places and partial moves are out of MVP scope. GAPS section 5 is a target requirement, not an MVP gate. | ADR 0004 (aggregate ownership) |
| C-02 | Context-sensitive words have no disambiguation procedure | MASTER_SPEC section 7/42, RFC-0001 section 7.1 (L223) and section 13 (L461) vs. syntax shown in MASTER_SPEC section 8 | C parse wins whenever the token stream is valid C; Crug reading requires the C parse to be impossible. | ADR 0002 |
| C-03 | Aggregates containing owned fields: ABI-transparent layout and no hidden state vs. the need to know whether a field was moved | RFC-0001 section 14 (L487) and section 22 (L666) vs. RUST_MEMORY_GAPS section 5/section 6 | No hidden moved bit. A join where drop correctness depends on runtime state is a compile error, not a hidden flag. | ADR 0004 |
| C-04 | Drop across non-local control flow is an escape ("MUST NOT claim ... through abnormal control flow it does not model") vs. a drop policy | RFC-0001 section 19 (L611), MASTER_SPEC section 14.8 vs. ROADMAP section 10/13 work items | Documented domain of guaranteed cleanup; everything outside it is a diagnostic under `-fcrug-strict`. | ADR 0005 |
| C-05 | P1 is defined by "the 1.0 production matrix published by the release RFC" which does not exist | RFC-0001 section 7.8 vs. ROADMAP section 2 | P1 is unclaimable until RFC-0010 exists and is published. | RFC-0010 (not written) |
| C-06 | 27 phases vs. 25 milestones; Phase 23 assigned to M23 in one section and M25 in another; Phase 24 has no milestone; M10 and M12 reach release from nowhere | ROADMAP section 28, section 30, section 33 | Phase-to-milestone mapping is authoritative only after EXIT-GATES.md is ratified. | docs/crug/EXIT-GATES.md |
| C-07 | Repository licence (MIT) vs. the GPLv3 obligation of a GCC fork and an unselected libcrug licence | LICENSE, ROADMAP Risk J, MASTER_SPEC section 80 | The MIT licence can only be correct while this repository contains no GCC-derived code. | DECISION-REQUIRED (owner) |
| C-08 | "Strict mode" is used as a load-bearing condition but is never defined | RFC-0001 L375, L617, L860, L1897; MASTER_SPEC section 6 | `-fcrug-strict` = ownership + borrow + all diagnostics that are warnings under hybrid become errors. Nothing else. | ADR 0005 (diagnostics part) |
| C-09 | Kernel corpus is required by K0 but exists in no artefact | RFC-0001 L229-233, L976 | K0 is unclaimable until a pinned corpus exists. | EXIT-GATES.md phase 0K |
| C-10 | Ownership copy may be "explicitly allowed" but no mechanism is named | RFC-0001 L243, L538 | MVP has no copy mechanism for owned values; release-and-readopt is the only escape. | ADR 0004 |
| C-11 | ADR numbering: MASTER_SPEC section 79 fixes 17 required file names, `docs/crug/adr/README.md` proposed a different topic order, and the grammar ADR has no required slot | MASTER_SPEC L4266-4283 vs. docs/crug/adr/README.md | A required topic keeps its required number; a new topic takes the next free number above 0017; no number is reused. The mapping is in `docs/crug/adr/README.md`. | ADR 0018 (grammar), `docs/crug/adr/README.md` |

## 5. Amendment rules

- A change to semantics requires an ADR and at least one test in
  `gcc/testsuite/crug.dg/`. A document edit alone is not a semantic change.
- Any edited normative paragraph carries an `Amended: YYYY-MM-DD (ADR-NNNN)`
  line so that line-number citations in older analysis remain traceable.
- The conflict register grows, never shrinks silently. A resolved conflict keeps
  its row and gains `resolved by`.

## 6. DECISION-REQUIRED (owner only)

1. Whether this repository becomes the GCC fork. If yes, C-07 resolves toward
   GPLv3 for all compiler code and the spec tree needs a separate licence grant.
2. Whether MVP tracks field-granular places (C-01) or is restricted as stated.
3. Whether the kernel qualification target is a specific version plus defconfig
   (C-09), and which one.
4. Whether ADR 0004 stays one decision record or is split into the required
   0003 (ownership metadata storage) and 0004 (ownership analysis layer) files
   (C-11). Splitting is cheaper now than after ratification.
