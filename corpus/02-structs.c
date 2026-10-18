struct point { int x; int y; };
struct rect { struct point a; struct point b; char name[8]; };

struct rect make_rect(int x, int y)
{
  struct rect r = { { x, y }, { x + 1, y + 1 }, "r" };
  return r;
}

int area(struct rect *r)
{
  return (r->b.x - r->a.x) * (r->b.y - r->a.y);
}
