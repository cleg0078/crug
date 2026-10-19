/* Crug feature flags: option closure over the dependencies in ADR 0018 D1,
   and the values the front end and the predefined macros consult.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "options.h"
#include "diagnostic.h"
#include "crug-common.h"

/* Active features after closure; see crug-common.h.  */

unsigned crug_features;

/* Read the -fcrug-* options, apply the dependencies of ADR 0018 D1, and
   report a combination that cannot have one meaning.  The driver never
   errors because a prerequisite is missing: every flag may be given alone,
   which is what EXIT-GATES phase 3 requires.  */

void
crug_apply_option_closure (void)
{
  unsigned features = 0;

  if (crug_flag_ownership)
    features |= CRUG_FEAT_OWNERSHIP;
  if (crug_flag_borrow)
    features |= CRUG_FEAT_BORROW;
  if (crug_flag_slices)
    features |= CRUG_FEAT_SLICES;
  if (crug_flag_result)
    features |= CRUG_FEAT_RESULT;
  if (crug_flag_adt)
    features |= CRUG_FEAT_ADT;
  if (crug_flag_goroutines)
    features |= CRUG_FEAT_GOROUTINES;
  if (crug_flag_channels)
    features |= CRUG_FEAT_CHANNELS;
  if (crug_flag_strict)
    features |= CRUG_FEAT_STRICT;
  if (crug_flag_kernel)
    features |= CRUG_FEAT_KERNEL;

  /* -fcrug-modern is every production, and is expanded here rather than
     listed in the option table so that its meaning has one definition.  */
  if (crug_flag_modern)
    features |= CRUG_FEAT_MODERN_SET;

  /* Dependencies.  Implying a feature never turns it off, and never
     produces an error, because each option is documented as usable alone.  */
  if (features & CRUG_FEAT_BORROW)
    features |= CRUG_FEAT_OWNERSHIP;
  if (features & CRUG_FEAT_CHANNELS)
    features |= CRUG_FEAT_GOROUTINES;
  if (features & CRUG_FEAT_RESULT)
    features |= CRUG_FEAT_ADT;
  if (features & CRUG_FEAT_STRICT)
    features |= CRUG_FEAT_OWNERSHIP | CRUG_FEAT_BORROW;

  /* Kernel mode removes the concurrency productions.  Asking for them in
     the same command line is contradictory, not a precedence rule.  */
  if (features & CRUG_FEAT_KERNEL)
    {
      if (features & (CRUG_FEAT_GOROUTINES | CRUG_FEAT_CHANNELS))
	error ("%<-fcrug-kernel%> removes %<-fcrug-goroutines%>, "
	       "%<-fcrug-channels%> and %<-fcrug-modern%>; "
	       "give one or the other");
      /* Kernel-mode drop lowering is a phase-5 non-goal, and emitting free()
	 into freestanding code would claim a runtime it does not have.  */
      if (features & (CRUG_FEAT_OWNERSHIP | CRUG_FEAT_BORROW))
	error ("%<-fcrug-kernel%> cannot be combined with %<-fcrug-ownership%> "
	       "or %<-fcrug-borrow%>; kernel-mode drop lowering arrives in "
	       "phase 19");
    }

  /* -fcrug-dump= takes a comma-separated list of dump kinds.  */
  if (crug_dump_kind != NULL)
    {
      const char *p = crug_dump_kind;
      while (*p != '\0')
	{
	  size_t n = strcspn (p, ",");
	  if (!((n == 9 && memcmp (p, "ownership", 9) == 0)
		|| (n == 4 && memcmp (p, "drop", 4) == 0)
		|| (n == 6 && memcmp (p, "borrow", 6) == 0)))
	    error ("unrecognized argument to %<-fcrug-dump=%>: %qs",
		   crug_dump_kind);
	  p += n;
	  if (*p == ',')
	    p++;
	}
    }

  crug_features = features;
}
