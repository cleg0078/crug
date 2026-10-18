/* Crug feature flags (ADR 0018 D1).

   Each feature has one -fcrug-* option in gcc/common.opt.  This file closes
   the accepted set over the dependencies that ADR 0018 D1 declares
   (-fcrug-borrow implies ownership, -fcrug-channels implies goroutines,
   -fcrug-result implies adt, -fcrug-strict implies ownership and borrow,
   -fcrug-modern enables everything, -fcrug-kernel removes the concurrency
   productions) and exposes the result to the front end.  The closure runs
   once, from c_common_post_options, before anything depends on it.  */

#ifndef GCC_CRUG_COMMON_H
#define GCC_CRUG_COMMON_H

/* One bit per production-bearing feature, in the order ADR 0018 D1 lists
   them.  Modern and kernel are modes, not features: modern means every bit
   here, and kernel is a mode that removes goroutines and channels.  */

#define CRUG_FEAT_OWNERSHIP	(1u << 0)
#define CRUG_FEAT_BORROW	(1u << 1)
#define CRUG_FEAT_SLICES	(1u << 2)
#define CRUG_FEAT_RESULT	(1u << 3)
#define CRUG_FEAT_ADT		(1u << 4)
#define CRUG_FEAT_GOROUTINES	(1u << 5)
#define CRUG_FEAT_CHANNELS	(1u << 6)
#define CRUG_FEAT_STRICT	(1u << 7)
#define CRUG_FEAT_KERNEL	(1u << 8)

/* What -fcrug-modern enables.  Kernel is deliberately not part of it: a
   kernel build must not gain goroutines or channels by accident.  */

#define CRUG_FEAT_MODERN_SET	(CRUG_FEAT_OWNERSHIP | CRUG_FEAT_BORROW \
				 | CRUG_FEAT_SLICES | CRUG_FEAT_RESULT \
				 | CRUG_FEAT_ADT | CRUG_FEAT_GOROUTINES \
				 | CRUG_FEAT_CHANNELS | CRUG_FEAT_STRICT)

/* The active features after closure.  Zero when every -fcrug-* option is
   off, which is the plain-C configuration that phase 1's identity gate
   compares with upstream GCC.  */

extern unsigned crug_features;

/* True when every bit in MASK is active.  */

inline bool
crug_feature_enabled (unsigned mask)
{
  return (crug_features & mask) == mask;
}

/* Close the accepted option set over the ADR 0018 D1 dependencies and
   report contradictory combinations.  Called once from
   c_common_post_options.  */

extern void crug_apply_option_closure (void);

#endif /* GCC_CRUG_COMMON_H */
