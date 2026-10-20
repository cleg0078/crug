/* crug-ADT-004: a unit variant takes no payload.  */
enum Message
{
  Text (char *),
  Quit
};

void f (void)
{
  enum Message m = Quit (1);
  (void) m;
}
