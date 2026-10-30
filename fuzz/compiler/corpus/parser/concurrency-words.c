/* Concurrency words are ordinary C identifiers.
 *
 * Crug adds no preprocessing token and no reserved word (RFC-0001 section 13,
 * ADR 0018 D1).  With only -fcrug-ownership enabled, go/scope/chan/channel/
 * send/recv/select must keep their plain C meaning, including as function
 * names, parameters, members and file-scope objects.
 */

struct concurrency
{
  int go;
  int scope;
  int chan;
  int channel;
  int send;
  int recv;
  int select;
};

int send = 1;
int recv = 2;
int channel = 3;

int
select (int go, int scope, int chan)
{
  struct concurrency c = { go, scope, chan, 4, 5, 6, 7 };
  return c.go + c.scope + c.chan + c.channel + c.send + c.recv + c.select
    + send + recv + channel + go + scope + chan;
}
