typedef int v4si __attribute__((vector_size(16)));

v4si add_vectors(v4si a, v4si b) { return a + b; }
int lane(v4si v) { return v[2]; }
