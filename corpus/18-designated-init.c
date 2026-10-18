struct cfg { int a; int b; const char *name; int values[4]; };

struct cfg config = {
  .b = 2,
  .name = "x",
  .values = { [1] = 10, [3] = 30 },
};

int read_config(void) { return config.a + config.b + config.values[3]; }
