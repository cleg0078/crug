/* Kernel-style GNU C: basic and extended inline asm (roadmap 7.13). */
static inline void kern_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__("rep; nop" ::: "memory");
#else
	__asm__ __volatile__("" ::: "memory");
#endif
}

static inline unsigned long kern_read_flags(void)
{
	unsigned long flags;
#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__("pushfq; popq %0" : "=r"(flags) :: "memory");
#else
	flags = 0;
#endif
	return flags;
}

int kern_use_inline_asm(void)
{
	kern_cpu_relax();
	return (int)(kern_read_flags() & 1UL);
}
