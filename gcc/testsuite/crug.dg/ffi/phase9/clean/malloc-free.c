/* Plain C that libc.crugffi annotates.  No ownership is created, so the
   object must be byte-identical with and without the sidecar (ADR 0007 D7).
   Both headers are included because an explicit sidecar must name functions
   the translation unit declares (ADR 0007 D5).  */
#include <stdlib.h>
#include <stdio.h>

void *get (unsigned long n) { return malloc (n); }
