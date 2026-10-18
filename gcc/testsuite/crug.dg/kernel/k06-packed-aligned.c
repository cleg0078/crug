/* Kernel-style GNU C: packed and aligned types (roadmap 7.9). */
struct kern_packed {
	unsigned char tag;
	unsigned int value;
} __attribute__((packed));

struct kern_aligned {
	unsigned char tag;
	unsigned int value;
} __attribute__((aligned(16)));

_Static_assert(sizeof(struct kern_packed) == 5, "packed layout changed");

int kern_use_packed_aligned(struct kern_packed *p)
{
	return p->tag + (int)p->value;
}

unsigned long kern_alignment(void)
{
	return __alignof__(struct kern_aligned);
}
