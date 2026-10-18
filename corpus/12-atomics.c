#include <stdatomic.h>

atomic_int counter;

int bump(void) { return atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed); }

int load(void) { return atomic_load_explicit(&counter, memory_order_acquire); }
