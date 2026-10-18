/* Kernel-style GNU C: attributes (roadmap 7.7). */
#define __kern_used __attribute__((used))
#define __kern_unused __attribute__((unused))
#define __kern_inline __attribute__((always_inline)) inline

static __kern_used int kern_used_data = 5;
static __kern_unused int kern_never_used(void)
{
	return 0;
}

static __kern_inline int kern_add(int a, int b)
{
	return a + b;
}

int kern_use_attributes(int x)
{
	return kern_add(x, kern_used_data) + kern_never_used();
}
