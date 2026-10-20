/* { dg-do compile } */
/* { dg-options "-fcrug-ownership" } */

/* A parse error can leave error_mark_node as the function body; the
   ownership pass must return without calling BIND_EXPR_BODY on it.
   Regression for the Phase 22 fuzz finding K-002 (an ICE, not a
   diagnostic).  */
int f (void) * x { return 0; } /* { dg-error "expected declaration specifiers" } */

/* { dg-prune-output "at end of input" } */
