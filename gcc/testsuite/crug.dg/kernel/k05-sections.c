/* Kernel-style GNU C: section placement (roadmap 7.8). */
#define __kern_init __attribute__((section(".text.kern_init")))
#define __kern_ro_after_init __attribute__((section(".rodata.kern")))
#define __kern_used __attribute__((used))

static __kern_init int kern_init_value(void)
{
	return 11;
}

static const int kern_ro_after __kern_ro_after_init __kern_used = 13;

int kern_use_sections(void)
{
	return kern_init_value() + kern_ro_after;
}
