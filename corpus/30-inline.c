static inline int square(int x) { return x * x; }
extern inline int cube(int x) { return x * square(x); }
int use_inline(int x) { return square(x) + cube(x); }
