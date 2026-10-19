void *bits (unsigned long i)
{
  void *p;
  unsafe { p = (void *) i; }
  return p;
}

unsigned long back (void *p)
{
  unsigned long i;
  unsafe { i = (unsigned long) p; }
  return i;
}
