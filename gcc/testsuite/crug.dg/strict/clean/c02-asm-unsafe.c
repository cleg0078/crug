void poke (void)
{
  unsafe { asm volatile ("" ::: "memory"); }
}
