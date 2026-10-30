/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-04: a file-scope owner is not tracked by the per-function
   ownership pass, so it would never be destroyed.  The declaration is
   rejected instead of silently doing nothing.  */

own int *gown; /* { dg-error "crug-OWN-014" } */
