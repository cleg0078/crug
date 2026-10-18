struct flags {
  unsigned a : 1;
  unsigned b : 2;
  signed c : 5;
  unsigned d : 8;
  unsigned e : 16;
};

unsigned read_b(struct flags *f) { return f->b; }
void set_c(struct flags *f, int v) { f->c = v; }
