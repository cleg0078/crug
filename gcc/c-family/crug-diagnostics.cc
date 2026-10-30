/* Crug diagnostics: attach the stable code as rule metadata.
   See crug-diagnostics.h and docs/crug/DIAGNOSTICS.md.  */

#define INCLUDE_VECTOR
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "intl.h"
#include "diagnostic.h"
#include "diagnostics/metadata.h"
#include "rich-location.h"
#include "crug-diagnostics.h"

/* Where a code is explained.  The text sinks only show this URL when the
   output is a terminal that supports it; SARIF carries it as helpUri.  */

static const char crug_diagnostics_url[]
  = "https://github.com/cleg0078/crug/blob/main/docs/crug/DIAGNOSTICS.md";

/* Emit KIND at LOC.  When CODE is non-null it becomes the diagnostic's
   rule, which is what the text and SARIF sinks render as the stable code.  */

static int crug_suppress_depth;

void
crug_diagnostics_suppress_push ()
{
  crug_suppress_depth++;
}

void
crug_diagnostics_suppress_pop ()
{
  if (crug_suppress_depth > 0)
    crug_suppress_depth--;
}

bool
crug_diagnostics_suppressed ()
{
  return crug_suppress_depth > 0;
}

static bool
crug_diagnostic_valist (location_t loc, diagnostics::kind kind, int option,
			const char *code, const char *gmsgid, va_list *ap)
{
  /* A probe pass must not report anything, and it must not change whether a
     warning would be printed.  */
  if (crug_suppress_depth > 0)
    return false;

  rich_location richloc (line_table, loc);

  if (!code)
    return emit_diagnostic_valist_meta (kind, &richloc, nullptr, option,
					gmsgid, ap);

  diagnostics::metadata::precanned_rule rule (code, crug_diagnostics_url);
  diagnostics::metadata md;
  md.add_rule (rule);
  return emit_diagnostic_valist_meta (kind, &richloc, &md, option,
				      gmsgid, ap);
}

void
crug_error_at (location_t loc, const char *code, const char *gmsgid, ...)
{
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  crug_diagnostic_valist (loc, diagnostics::kind::error, -1, code, gmsgid,
			  &ap);
  va_end (ap);
}

bool
crug_warning_at (location_t loc, int option, const char *code,
		 const char *gmsgid, ...)
{
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  bool ret = crug_diagnostic_valist (loc, diagnostics::kind::warning, option,
				     code, gmsgid, &ap);
  va_end (ap);
  return ret;
}

void
crug_note_at (location_t loc, const char *gmsgid, ...)
{
  auto_diagnostic_group d;
  va_list ap;
  va_start (ap, gmsgid);
  crug_diagnostic_valist (loc, diagnostics::kind::note, -1, nullptr, gmsgid,
			  &ap);
  va_end (ap);
}
