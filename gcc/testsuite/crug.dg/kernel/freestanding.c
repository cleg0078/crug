/* Phase 2 gate path: freestanding kernel-style translation unit.
 * Compiles with: -fcrug-kernel -ffreestanding -nostdlib -c
 * No libc headers, no libc calls, no constructors, no Crug runtime. */
#define KERN_LOG_SIZE 64

struct kern_log {
	unsigned int head;
	unsigned int tail;
	char data[KERN_LOG_SIZE];
};

static struct kern_log kern_log;

static unsigned int kern_next(unsigned int index)
{
	return (index + 1) % KERN_LOG_SIZE;
}

int kern_log_put(char c)
{
	unsigned int next = kern_next(kern_log.head);

	if (next == kern_log.tail)
		return -1;
	kern_log.data[kern_log.head] = c;
	kern_log.head = next;
	return 0;
}

int kern_log_get(char *c)
{
	if (kern_log.head == kern_log.tail)
		return -1;
	*c = kern_log.data[kern_log.tail];
	kern_log.tail = kern_next(kern_log.tail);
	return 0;
}
