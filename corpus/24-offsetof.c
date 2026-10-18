#include <stddef.h>

struct inner { int a; int b; };
struct outer { char tag; struct inner in; long tail; };

size_t inner_offset(void) { return offsetof(struct outer, in); }

struct inner *container(struct outer *o) { return &o->in; }
