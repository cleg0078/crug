/* { dg-do compile } */
/* { dg-options "-O2 -fcrug-ownership -Wno-crug-unadopted-own" } */

/* fix-p3 OWN-05: an 'own' array of records would own its elements only on
   paper; the form is rejected (crug-TYPE-015).  */

struct point { int x; };

void array_of_records (void)
{
  own struct point a[2]; /* { dg-error "crug-TYPE-015" } */
}
