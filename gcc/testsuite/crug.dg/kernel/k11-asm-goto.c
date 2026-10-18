/* Kernel-style GNU C: asm goto (roadmap 7.14). */
int kern_use_asm_goto(int value)
{
	int taken = 0;

#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__ goto("testl %0, %0\n\tjz %l[kern_zero]"
				  :
				  : "r"(value)
				  :
				  : kern_zero);
#else
	if (value == 0)
		goto kern_zero;
#endif
	taken = 1;
kern_zero:
	return taken;
}
