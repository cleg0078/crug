enum color { RED, GREEN = 5, BLUE };

enum color next(enum color c)
{
  switch (c) {
  case RED: return GREEN;
  case GREEN: return BLUE;
  default: return RED;
  }
}

int value(enum color c) { return (int)c; }
