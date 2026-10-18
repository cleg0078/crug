int bucket(int c)
{
  switch (c) {
  case 'a' ... 'z': return 1;
  case 'A' ... 'Z': return 2;
  case '0' ... '9': return 3;
  default: return 0;
  }
}
