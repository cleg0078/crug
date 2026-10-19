/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* ADR 0003 D3 and ADR 0004 D3: the qualifier is erased at every ABI
   boundary, so it may not change size or alignment.  */

struct Foo { int x; };
typedef struct Foo Foo;

_Static_assert (sizeof (own Foo *) == sizeof (Foo *),
		"own must not change size");
_Static_assert (_Alignof (own Foo *) == _Alignof (Foo *),
		"own must not change alignment");
_Static_assert (sizeof (own char **) == sizeof (char **),
		"own on a pointer-sized object must not change size");

extern void takes_plain (Foo *);
extern void takes_own (own Foo *);
extern own Foo *returns_own (void);

/* A qualifier may not appear in the type of a function pointer.  */
void (*plain_fp) (Foo *) = takes_own;
