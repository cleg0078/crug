/* 18-channels.c - channels and select across tasks.
   Copyright (C) 2026 the Crug authors.  GPLv3+; see LICENSE.  */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void send_value (chan int out, int v)
{
  send (out, v);
}

static void send_text (chan own char *out)
{
  own char *text = adopt (malloc (8), free);
  strcpy (text, "hello");
  send (out, move (text));
}

int main (void)
{
  chan int numbers = channel (3);

  scope {
    go send_value (numbers, 10);
    go send_value (numbers, 20);
    go send_value (numbers, 30);
  }

  int sum = 0;
  for (int i = 0; i < 3; i++)
    sum += recv (numbers);

  chan int left = channel (1);
  chan int right = channel (1);

  scope {
    go send_value (left, 1);
    go send_value (right, 2);
  }

  int picked = 0;
  select {
  case v = recv (left):
    picked = v;
  case w = recv (right):
    picked = w;
  }

  chan own char *texts = channel (1);
  scope {
    go send_text (texts);
  }
  own char *text = recv (texts);
  int size = (int) sizeof (chan int);

  printf ("sum=%d\n", sum);
  printf ("picked=%d\n", picked);
  printf ("text=%s size=%d\n", text, size);
  release (text);
  return 0;
}
