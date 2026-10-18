/* Contextual words are ordinary C identifiers.
 *
 * Crug adds no preprocessing token and no reserved word (RFC-0001 section 13,
 * ADR 0018 D1).  With every feature flag on, these declarations must still
 * parse exactly as C, which is what makes the flags-off identity of Phase 1
 * extendable: a C program that uses any of these names keeps its meaning.
 *
 * Phase 3 installs the option set only; Phase 4 and later give some of the
 * words productions in specific positions, and must leave this file valid.
 */

struct words
{
  int own;
  int ref;
  int mut;
  int raw;
};

int own = 1;
int ref = 2;
int mut = 3;
int raw = 4;
int move = 5;
int borrow = 6;
int borrow_mut = 7;
int adopt = 8;
int release = 9;
int drop = 10;
int go = 11;
int scope = 12;
int chan = 13;
int select = 14;
int match = 15;
int some = 16;
int none = 17;
int ok = 18;
int err = 19;
int unsafe = 20;

int
sum_words (struct words *w)
{
  return w->own + w->ref + w->mut + w->raw + move + borrow + borrow_mut
    + adopt + release + drop + go + scope + chan + select + match + some
    + none + ok + err + unsafe;
}
