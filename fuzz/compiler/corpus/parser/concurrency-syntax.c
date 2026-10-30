/* Crug concurrency syntax with the concurrency flags off.
 *
 * The parser target enables only -fcrug-ownership, so chan/channel/send/
 * recv/select/go/scope are not productions here.  The program must be
 * rejected cleanly rather than crash; fuzzing its error recovery is the
 * point of keeping it in the parser corpus.
 */

extern void work (int);
extern int compute (void);

int
main (void)
{
  chan int c = channel (1);
  scope {
    go work (1);
    send (c, compute ());
    int v = recv (c);
    select {
      case 1:
        break;
      default:
        break;
    }
  }
  return 0;
}
