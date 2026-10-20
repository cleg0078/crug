/* crug-ADT-003: a payload variant cannot be named without its payload.  */
enum Message
{
  Text (char *),
  Quit
};

void f (void)
{
  enum Message m = Text;
  (void) m;
}
