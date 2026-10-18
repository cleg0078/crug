/* Kernel-style GNU C: typeof and __typeof__ (roadmap 7.5). */
static int kern_counter;

typeof(kern_counter) kern_get(void)
{
	return kern_counter;
}

__typeof__(kern_counter) kern_get2(void)
{
	return kern_counter;
}

int kern_use_typeof(void)
{
	__typeof__(kern_counter) x = 3;
	return kern_get() + kern_get2() + x;
}
