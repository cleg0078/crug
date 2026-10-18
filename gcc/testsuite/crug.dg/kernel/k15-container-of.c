/* Kernel-style GNU C: container_of (roadmap 7.18). */
#define kern_container_of(ptr, type, member) \
	((type *)((char *)(ptr) - __builtin_offsetof(type, member)))

struct kern_item {
	int id;
	unsigned long flags;
	struct kern_item *next;
};

int kern_use_container_of(struct kern_item *item)
{
	struct kern_item *next = item->next;
	unsigned long *flags = &item->flags;
	struct kern_item *back = kern_container_of(flags, struct kern_item, flags);

	return back == item ? (int)item->flags : 0;
}
