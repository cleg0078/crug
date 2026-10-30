/* Crug diagnostics: emit a message with its stable code attached.

   The registry in docs/crug/DIAGNOSTICS.md fixes one crug-<AREA>-<NNN> code
   per diagnostic class.  The code is attached to the diagnostic as rule
   metadata, so the text sinks append " [crug-...]" (the format the tests
   and users expect) and the SARIF sink can carry the code in ruleId.  The
   message itself never contains the code: one diagnostic, one place that
   owns the code.

   Call sites that want to explain where a value was moved, adopted or
   borrowed should wrap the error and its notes in an auto_diagnostic_group
   so that the notes are nested under the error.  */

#ifndef GCC_CRUG_DIAGNOSTICS_H
#define GCC_CRUG_DIAGNOSTICS_H

#include "tree.h"

/* Emit an error at LOC whose message is GMSGID with the stable CODE
   attached.  */

extern void crug_error_at (location_t loc, const char *code,
			   const char *gmsgid, ...)
  ATTRIBUTE_GCC_DIAG (3, 4);

/* As above, but a warning controlled by OPTION.  Returns true if the
   warning was printed.  */

extern bool crug_warning_at (location_t loc, int option, const char *code,
			     const char *gmsgid, ...)
  ATTRIBUTE_GCC_DIAG (4, 5);

/* A note at LOC without a code, for origin explanations.  */

extern void crug_note_at (location_t loc, const char *gmsgid, ...)
  ATTRIBUTE_GCC_DIAG (2, 3);

/* A probe pass re-analyses statements to learn the state they leave behind
   without reporting anything twice.  While a suppression scope is active the
   three entry points above emit nothing and return false, and the analysis
   also suppresses its dump output and its cross-pass registrations, because
   the real pass owns them.  Scopes nest.  */

extern void crug_diagnostics_suppress_push ();
extern void crug_diagnostics_suppress_pop ();
extern bool crug_diagnostics_suppressed ();

#endif /* GCC_CRUG_DIAGNOSTICS_H */
