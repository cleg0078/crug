/* pal_contract.h - the backend contract suite of the Crug PAL.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.

   The suite lives in libcrug/core on purpose: it is the proof that the
   scheduler builds against the contract alone.  It therefore includes
   <crug_pal.h> and nothing else from the platform, and it reports through a
   callback instead of writing to a stream itself (ADR 0011 D5).
*/

#ifndef CRUG_PAL_CONTRACT_H
#define CRUG_PAL_CONTRACT_H

#include "crug_pal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum crug_pal_verdict
{
  CRUG_PAL_CASE_OK = 0,
  CRUG_PAL_CASE_SKIP = 1,  /* a capability the contract lets a backend decline */
  CRUG_PAL_CASE_FAIL = 2
} crug_pal_verdict;

typedef void (*crug_pal_reporter) (void *ctx, const char *case_name,
                                  crug_pal_verdict verdict,
                                  const char *detail);

struct crug_pal_contract_summary
{
  unsigned cases;
  unsigned ok;
  unsigned skipped;
  unsigned failed;
};

/* Run every contract case once.  detail is a static string, never NULL.  */
void crug_pal_contract_run (crug_pal_reporter report, void *ctx,
                            struct crug_pal_contract_summary *summary);

#ifdef __cplusplus
}
#endif

#endif /* CRUG_PAL_CONTRACT_H */
