# libcrug/arch

CPU and ABI specific context switching. Empty in Phase 13: the PAL is about
operating system services, and no task context exists yet.

Phase 14 adds the x86-64 System V context switch here, together with the ADR
that reviews it against the ABI (EXIT-GATES phase 14 prerequisite). The
portability rule is split along the two axes of RFC-0001 section 36:

```text
Operating system  ->  libcrug/pal/<os>/
CPU and ABI       ->  libcrug/arch/<arch>/<abi>/
```

`libcrug/core` must never contain this code, and `grep -R 'asm' libcrug/core`
is part of the phase 13 gate.
