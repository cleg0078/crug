# ADR 0017 - Kernel Ownership Policy: Kernel Code Is Plain GNU C in v1

- **Status:** Proposed (2026-10-20)
- **Ratified:** not yet; awaits owner ratification.
- **Related:** ADR 0002 (contextual keywords), ADR 0005 (drop lowering),
  ADR 0006 (C ABI policy), ADR 0018 (surface grammar),
  docs/crug/KERNEL-BASELINE.md, docs/crug/KERNEL-COMPATIBILITY.md,
  docs/crug/EXIT-GATES.md phase 19, kernel/KERNEL-CORPUS.md,
  MASTER_SPEC stage 8K, roadmap section 33.

## Context

The master specification's stage 8K offers *optional* ownership semantics for
kernel code, and the roadmap leaves the option open. Phase 19 builds and boots
the pinned Linux 6.18.57 tree with `crugc`, and its non-goals explicitly exclude
kernel ownership contracts. That makes the question decidable now: what does a
Crug compiler promise about ownership in kernel code?

The kernel is a poor fit for the ownership rules the earlier phases implement:

- there is no libc allocation catalogue to derive destructors from; kernel
  memory comes from `kmalloc`/`kzalloc`/`vmalloc`/`kmem_cache_alloc` and is
  freed by `kfree`/`vfree`/`kmem_cache_free`, often on another CPU or in a
  deferred context;
- lifetimes are frequently not lexical: RCU grace periods, reference counts,
  work queues, timers and socket destructors all outlive the block that created
  the object;
- the kernel dialect is plain GNU C with freestanding headers, and Phase 2
  already measures that it builds without any Crug runtime or hidden state.

An ownership checker that guessed destructors from kernel allocation calls
would be unsound exactly where the kernel is hardest, and a checker that
rejected every unproven kernel object would reject most of the tree.

## Decision

**D1. Kernel code is plain GNU C.** Under `-fcrug-kernel` the compiler performs
no ownership analysis, inserts no drop, and accepts no Crug construct that
requires one. Blocking `-fcrug-kernel` together with `-fcrug-ownership`,
`-fcrug-borrow`, `-fcrug-goroutines`, `-fcrug-channels` or `-fcrug-modern` is
part of the option closure and stays a hard error.

**D2. No kernel ownership contracts in v1.** There is no kernel `.crugffi`
catalogue, no `kmalloc`-`kfree` destructor mapping, no annotated kernel
interface, and no ownership rule that applies only to kernel sources. A later
phase that wants them needs a superseding ADR, not a new flag.

**D3. The kernel claim is a compatibility claim, not a safety claim.** Phase 19
shows that plain GNU C kernel sources compile with the same driver and generate
the same objects as the wrapped GCC, and that the result boots. It makes no
statement about ownership, borrowing or data races in kernel code, and no
`class K0` result rests on it.

**D4. A future kernel ownership increment must be additive and explicit.** It
must name the allocation/release pairs it understands, the lock and RCU
lifetime rules it models, the diagnostics it adds, and it must change no object
byte unless a flag asks for the analysis. Until such an ADR exists, kernel
sources use `raw` pointers and manual discipline, as they do today.

## Consequences

- `crugc` can be used as a drop-in kernel compiler without parsing new
  constructs, which is what stages K1-K7 exercise.
- Crug's ownership guarantees remain a property of Crug code; a kernel module
  that mixes Crug and plain GNU C gets the ownership rules only in the Crug
  translation unit, and an `own` pointer that leaves it becomes a plain
  pointer, as at any C boundary (ADR 0006).
- Kernel bugs that ownership would catch (double free, use after free) remain
  out of the compiler's reach in kernel code; the kernel's own tooling stays
  the defence.
- The decision is small enough to reverse: enabling ownership for a kernel
  subtree later is a superseding ADR plus a corpus, not a redesign.

## Evidence

- `scripts/kernel-stage.sh` K1-K7 pass on Linux 6.18.57 (x86_64 `defconfig`
  and the aarch64 object subset).
- The Phase 19 object diff (`scripts/kernel-object-diff.sh`) finds all 438
  pinned corpus objects byte-identical to the wrapped GCC's, except the object
  that embeds the build timestamp (recorded in `kernel/object-diff-allow.txt`).
- K6 boots the kernel under QEMU to a freestanding `/init` and powers off.
