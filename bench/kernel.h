/* Shared declarations for the benchmark matrix.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Each kernel is split into a kernel translation unit (compiled once per arm:
   plain C, C+restrict, Crug) and a plain-C main translation unit that is the
   same for every arm.  The split matters for two reasons:

   - the Crug arm and the hand-written restrict arm can be compared as object
     code: the kernel function is the only thing that differs, so byte identity
     is meaningful;
   - the call site stays ordinary C, which is exactly the contract: a mut
     parameter is an exclusive borrow and the C ABI carries no extra argument.  */

#ifndef CRUG_BENCH_KERNEL_H
#define CRUG_BENCH_KERNEL_H

void bench_vadd (int *d, const int *a, const int *b);
void bench_stencil (int *d, const int *a);
void bench_strcopy (char *d, const char *s);

struct bench_node
{
  long v;
  struct bench_node *next;
};

long bench_list (const struct bench_node *head);

#endif /* CRUG_BENCH_KERNEL_H */
