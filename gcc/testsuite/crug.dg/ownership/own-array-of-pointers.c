/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-05: element-wise destruction of an 'own' array was never
   implemented, so the form was accepted but every use was diagnosed as an
   empty owned value.  It is now rejected at the declaration.  */

void array_of_pointers (void)
{
  own int *a[4]; /* { dg-error "crug-TYPE-015" } */
}
