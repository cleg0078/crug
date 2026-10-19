/* Plain C that libc.crugffi annotates; see malloc-free.c.  */
#include <stdlib.h>
#include <stdio.h>

FILE *open_it (const char *p) { return fopen (p, "r"); }
