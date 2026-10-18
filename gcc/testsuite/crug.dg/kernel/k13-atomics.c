/* Kernel-style GNU C: builtin atomics and barriers (roadmap 7.16). */
static int kern_counter;

int kern_use_atomics(void)
{
	int old = __atomic_fetch_add(&kern_counter, 1, __ATOMIC_ACQ_REL);
	int value = __atomic_load_n(&kern_counter, __ATOMIC_ACQUIRE);

	__atomic_store_n(&kern_counter, value + 2, __ATOMIC_RELEASE);
	__sync_synchronize();
	return old;
}

int kern_atomic_cas(int *p, int expected, int desired)
{
	return __atomic_compare_exchange_n(p, &expected, desired, 0,
					   __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}
