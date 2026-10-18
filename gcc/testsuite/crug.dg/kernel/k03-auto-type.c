/* Kernel-style GNU C: __auto_type (roadmap 7.6). */
struct kern_dev {
	int id;
	int flags;
};

int kern_use_auto_type(void)
{
	__auto_type dev = (struct kern_dev){ .id = 7, .flags = 1 };
	__auto_type p = &dev;
	return p->id + p->flags;
}
