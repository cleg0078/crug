/* SQLite: sqlite3_open_v2 hands out a handle that sqlite3_close consumes.
   The wrappers are plain C; the sidecar tells Crug who owns the handle, so
   the owned variable is closed by drop insertion exactly once.  */

#include <stdio.h>


typedef struct sqlite3 sqlite3;

extern int sqlite3_open_v2 (const char *name, sqlite3 **db, int flags,
			    const char *vfs);
extern int sqlite3_close (sqlite3 *db);

static int opens;
static int closes;

sqlite3 *my_open (void)
{
  sqlite3 *db = 0;
  if (sqlite3_open_v2 (":memory:", &db, 6, 0) != 0)
    return 0;
  opens++;
  return db;
}

void my_close (sqlite3 *db)
{
  if (db == 0)
    return;
  closes++;
  sqlite3_close (db);
}

int main (void)
{
  int ok;
  {
    own sqlite3 *db = my_open ();
    ok = (db != 0);
  }
  if (!ok)
    return 1;
  printf ("ok sqlite opens=%d closes=%d\n", opens, closes);
  return (opens == 1 && closes == 1) ? 0 : 2;
}
