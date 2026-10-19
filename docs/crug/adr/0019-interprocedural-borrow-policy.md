# ADR 0019 - Interprocedural Borrow Policy: Depth Zero, Calls Fail Closed

- **Status:** Accepted (2026-10-19)
- **Ratified:** 2026-10-19 by CLEG0078 (project owner), as the Phase 7 planning decision.
- **Related:** ADR 0003 (ownership metadata is static), ADR 0006 (C ABI policy),
  ADR 0018 (surface grammar), RFC-0001 sections 18, 23 and 24,
  docs/crug/BORROW.md, docs/crug/EXIT-GATES.md phase 7.

## Context

Phase 6 checks borrows lexically inside one function body. Two questions are
left for every function boundary:

1. What happens to a borrow passed as an argument?
2. What happens to a borrow that is returned, or stored somewhere that outlives
   the call?

The roadmap asks for "limited interprocedural summaries" and never sets the
limit. A summary-based checker (compute, per function, which parameters are
borrowed and for how long, then check call sites against it) is the natural
next step, but it needs a call graph, recursion handling and a fixpoint over
the program, and it makes the checker's verdict depend on code it may not see.
A wrong summary silently accepts an escaping borrow, which is exactly the
class of bug the borrow checker exists to prevent.

## Decision

**D1. Summary depth is zero.** No function is summarized for borrow purposes.
The checker's knowledge of a callee is limited to the declared types of its
parameters and its return type, both visible in the current translation unit.

**D2. A borrow crosses a function boundary only as an argument to a parameter
declared `ref` or `mut`.** The parameter is the callee's borrow for the whole
call; the callee may use it and may not store it (D3). Passing a borrow to a
parameter without a borrow qualifier is rejected: `crug-BORROW-020`, "a borrow
may only be passed to a `ref` or `mut` parameter".

**D3. A borrow is an automatic variable.** It may not be stored in an object,
a static, or memory reached through a pointer: `crug-BORROW-018`. The check
is syntactic and conservative: any assignment of a borrow value to a
`COMPONENT_REF`, `ARRAY_REF`, `INDIRECT_REF` or `MEM_REF` is rejected, and so
is a file-scope borrow declaration.

**D4. A borrow may not be returned.** `crug-BORROW-019`, reported at the
return with a note at the borrow's origin. The function that creates a borrow
is the function in which it ends.

**D5. No inference across calls.** A call never extends a borrow's lifetime and
never ends one. If a value is passed by borrow and the caller also wants to
use it afterwards, it is still borrowed after the call for the rest of the
last-use range; that is the caller's business, and it is what the in-function
checker already computes.

## What this forbids

- Passing a borrow to `free`, `memcpy`, `strlen` or any other plain-pointer
  C function. FFI calls with ownership and borrow contracts arrive with
  `.crugffi` (Phase 9); until then such calls need `release`/`adopt` or a copy.
- Returning a pointer to a callee's local through a borrow (already impossible:
  the referent must be an owned object or another borrow).
- "Helper returns a borrow of my object" APIs. They can be written later, when
  a summary format exists, but each such API needs a contract.
- A borrow hidden behind `void *` or an integer, which is an unmanaged
  operation and therefore `unsafe`/strict territory (Phase 8), not a borrow.

## Consequences

- The rule fails closed: every construct the checker cannot follow is rejected,
  so a missing summary can only cause a false positive, never a false negative.
- The cost is real: borrows cannot reach libc until Phase 9. The documented
  workaround is to keep the owning object and pass plain pointers inside the
  same function, or to wait for the FFI contracts.
- Loops follow from D5: an argument passed by borrow inside a loop is a borrow
  for the call, and the loop's conservative liveness rule (ADR 0020) applies.

## Evidence

- `gcc/testsuite/crug.dg/borrow/escape-return.c` (019), `e18-static-borrow.c`
  and `e18-member-store.c` (018), `e20-plain-call.c` (020),
  `r17-return-borrow.c` (001 for a `ref` return type),
  `b04-param-borrows.c` and `b05-borrow-param-reborrow.c` (accepted cases).
- `scripts/check-borrow-policy.sh` requires 018/019/020 among its codes.

## Alternatives rejected

- **Full summaries now.** Rejected: no call graph, no recursion story, and a
  wrong summary is a soundness hole rather than a missing feature.
- **Accept and ignore borrows at calls.** Rejected: silently accepts an escape.
- **Reject all borrows at calls.** Rejected: `ref`/`mut` parameters are the
  language's way to write a borrow-taking API; banning them would remove the
  main use of borrows.
