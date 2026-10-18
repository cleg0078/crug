struct __attribute__((packed)) packed_pair { char c; int i; };

struct aligned_item { char c; } __attribute__((aligned(16)));

__attribute__((section(".data.crug"))) int in_section = 7;

__attribute__((used)) static int kept(void) { return 1; }

__attribute__((noreturn)) void stop(void);

int read_packed(struct packed_pair *p) { return p->i; }
