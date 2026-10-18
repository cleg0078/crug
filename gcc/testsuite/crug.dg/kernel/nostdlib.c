/* Phase 2: compiles and links conceptually without a C library.
 * No undefined libc symbols may be referenced by this object. */
#define KERN_PAGE_SIZE 4096UL
#define KERN_ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

unsigned long kern_align_up(unsigned long value)
{
	return KERN_ALIGN_UP(value, KERN_PAGE_SIZE);
}

unsigned long kern_pages(unsigned long bytes)
{
	return kern_align_up(bytes) / KERN_PAGE_SIZE;
}

void kern_zero_words(void *ptr, unsigned long words)
{
	unsigned long *p = (unsigned long *)ptr;
	unsigned long i;

	for (i = 0; i < words; i++)
		p[i] = 0;
}
