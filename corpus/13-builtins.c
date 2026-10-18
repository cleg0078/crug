int clz(unsigned x) { return __builtin_clz(x); }
int popcount(unsigned x) { return __builtin_popcount(x); }
unsigned byteswap(unsigned x) { return __builtin_bswap32(x); }
long expect(long x) { if (__builtin_expect(x == 0, 0)) return -x; return x; }
