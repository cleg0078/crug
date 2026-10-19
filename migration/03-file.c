/* Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */
/* Migration sample: fopen/fclose from a .crugffi contract.  The sidecar
   says fopen returns an owned FILE with destructor fclose, so c2crug can
   turn the handle into an owned value and the fclose into drop. */

#include <stdio.h>
#include <stdlib.h>

static int
write_sample (void)
{
  FILE *out = fopen ("migration-sample.txt", "w");
  if (out == NULL)
    return 1;

  fputs ("c2crug file sample\n", out);

  fclose (out);
  return 0;
}

static int
read_sample (void)
{
  char line[64];
  FILE *in = fopen ("migration-sample.txt", "r");
  if (in == NULL)
    return 1;

  (void) fgets (line, sizeof line, in);

  printf ("03-file %s", line);

  fclose (in);
  return 0;
}

int
main (void)
{
  int rc = write_sample ();
  if (rc == 0)
    rc = read_sample ();

  remove ("migration-sample.txt");
  return rc;
}
