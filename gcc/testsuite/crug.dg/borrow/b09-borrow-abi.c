/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-borrow -Wno-crug-unadopted-own" } */
_Static_assert (sizeof (ref struct Foo *) == sizeof (struct Foo *),
		"a shared borrow has pointer size");
_Static_assert (sizeof (mut struct Foo *) == sizeof (struct Foo *),
		"a mutable borrow has pointer size");
_Static_assert (_Alignof (ref struct Foo *) == _Alignof (struct Foo *),
		"a shared borrow has pointer alignment");
_Static_assert (_Alignof (mut struct Foo *) == _Alignof (struct Foo *),
		"a mutable borrow has pointer alignment");
void f (ref struct Foo *r, mut struct Foo *m)
{
  (void) r;
  (void) m;
}
