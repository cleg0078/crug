/* pal_contract_main.c - the runner for the Crug PAL contract suite.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   Prints one line per case and a summary, and exits non-zero if any case
   failed or if the suite produced no cases at all.  The backend name comes
   from the make variable (CRUG_PAL_BACKEND) so a log names its port.
*/

#include "pal_contract.h"

#include <stdio.h>

#ifndef CRUG_PAL_BACKEND
#define CRUG_PAL_BACKEND "unknown"
#endif

static void
print_case (void *ctx, const char *name, crug_pal_verdict verdict,
            const char *detail)
{
  (void) ctx;
  if (verdict == CRUG_PAL_CASE_OK)
    printf ("case %s: ok\n", name);
  else if (verdict == CRUG_PAL_CASE_SKIP)
    printf ("case %s: skip (%s)\n", name, detail);
  else
    printf ("case %s: FAIL (%s)\n", name, detail);
}

int
main (void)
{
  struct crug_pal_contract_summary summary;

  crug_pal_contract_run (print_case, NULL, &summary);
  printf ("pal-contract[%s]: %u cases, %u failed, %u skipped\n",
          CRUG_PAL_BACKEND, summary.cases, summary.failed, summary.skipped);
  return (summary.failed == 0 && summary.cases > 0) ? 0 : 1;
}
