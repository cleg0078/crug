/* Kernel-style GNU C: zero-length arrays (roadmap 7.10). */
struct kern_msg {
	unsigned int len;
	unsigned int type;
	unsigned char payload[0];
};

struct kern_msg_hdr {
	unsigned int room;
	unsigned char data[0];
};

int kern_use_zero_length(struct kern_msg *m)
{
	return (int)m->len + (int)m->type;
}
