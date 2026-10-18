struct sized { char data[16]; };

_Static_assert(sizeof(struct sized) == 16, "sized must be 16 bytes");
_Static_assert(sizeof(int) == 4, "int must be 4 bytes");

int size_of_sized(void) { return (int)sizeof(struct sized); }
