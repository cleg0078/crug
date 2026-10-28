/* `returns` without `owned` is accepted and names an unmanaged return;
   `nullable` is recorded only and `drop` names no destructor (ADR 0007 A-3).
   Both spellings have shipped since Phase 9 and must keep compiling.  */

int live_count (void);
void nothing (void);
int maybe (void);
int ignored (void);

int
main (void)
{
  return 0;
}
