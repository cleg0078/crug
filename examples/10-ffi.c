/* Phase 9: a contract from a sidecar.  libc.crugffi says fopen returns an
   owned FILE * and fclose consumes one, so the compiler destroys the FILE at
   the end of its scope and rejects a plain fclose of an owned value.

   build: crugc -fcrug-ffi=gcc/testsuite/crug.dg/ffi/libc.crugffi examples/10-ffi.c -o build/ffi-10
   run:   ./build/ffi-10
   check: crugc -fcrug-ffi=gcc/testsuite/crug.dg/ffi/libc.crugffi -fsyntax-only examples/10-ffi.c
*/

#include <stdio.h>
#include <stdlib.h>

static int count_lines (FILE *f)
{
  int c;
  int lines = 0;

  while ((c = fgetc (f)) != EOF)
    if (c == '\n')
      lines++;
  return lines;
}

int main (void)
{
  int lines;

  {
    own FILE *f = fopen ("/etc/hostname", "r");
    if (f == 0)
      {
        puts ("cannot open /etc/hostname");
        return 1;
      }
    lines = count_lines (f);
  }

  printf ("read %d newline(s); the FILE was closed by the contract\n", lines);

  {
    own void *buffer = malloc (64);
    if (buffer == 0)
      return 1;
    printf ("allocated %d bytes; freed by the contract\n", 64);
  }

  return 0;
}
