/* Kernel-style GNU C: flexible array members (roadmap 7.11). */
struct kern_blob {
	unsigned int len;
	unsigned char data[];
};

struct kern_ring {
	unsigned int head;
	unsigned int tail;
	int slots[];
};

int kern_use_flexible(struct kern_blob *b)
{
	return b->len == 0 ? 0 : b->data[0];
}
