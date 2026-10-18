/* Phase 2: an object that must not reference the Crug runtime.
 * Scalar arithmetic only; no aggregate copies, no libc calls. */
static unsigned int kern_state = 17;

unsigned int kern_mix(unsigned int value)
{
	unsigned int x = value ^ kern_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	kern_state = x;
	return x;
}

unsigned int kern_checksum(const unsigned char *data, unsigned int len)
{
	unsigned int sum = 0;
	unsigned int i;

	for (i = 0; i < len; i++)
		sum = (sum << 1) ^ data[i];
	return sum;
}
