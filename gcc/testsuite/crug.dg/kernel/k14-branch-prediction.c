/* Kernel-style GNU C: branch prediction hints (roadmap 7.17). */
#define kern_likely(x) __builtin_expect(!!(x), 1)
#define kern_unlikely(x) __builtin_expect(!!(x), 0)

int kern_use_branch_prediction(int value)
{
	if (kern_unlikely(value < 0))
		return -1;
	if (kern_likely(value == 0))
		return 0;
	return value;
}
