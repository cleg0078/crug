/* Kernel-style GNU C: volatile MMIO and barriers (roadmap 7.15). */
struct kern_regs {
	volatile unsigned int status;
	volatile unsigned int command;
	volatile unsigned int data;
};

static inline void kern_write_barrier(void)
{
	__asm__ __volatile__("" ::: "memory");
}

unsigned int kern_use_mmio(volatile struct kern_regs *regs)
{
	unsigned int status;

	regs->command = 1;
	kern_write_barrier();
	status = regs->status;
	kern_write_barrier();
	return status;
}
